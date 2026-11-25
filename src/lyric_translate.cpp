#include "stdafx.h"

#include <cctype>
#include <cstdlib>

#include "cJSON.h"

#include "logging.h"
#include "lyric_translate.h"
#include "preferences.h"
#include "ui_hooks.h"

static bool line_has_translation(const LyricDataLine& line)
{
    return line.text.find(_T('\n')) != std::tstring::npos;
}

bool lyrics_are_translated(const LyricData& lyrics)
{
    for(const LyricDataLine& line : lyrics.lines)
    {
        if(line_has_translation(line)) return true;
    }
    return false;
}

// ponytail: "original text is everything before the first newline" assumes one
// stacked line becomes original+translation. A hand-written LRC that stacks two
// real lyric lines under one timestamp would lose the second one; the confirm
// dialog in the context menu is the guard against that. Upgrade path: store an
// explicit "[id:...]" marker tag if this ever bites.
static std::string original_text(const LyricDataLine& line)
{
    std::string text = from_tstring(line.text);
    const size_t newline = text.find('\n');
    if(newline != std::string::npos)
    {
        text.resize(newline);
    }
    return text;
}

LyricData lyrics_without_translation(const LyricData& lyrics)
{
    LyricData result = lyrics;
    for(LyricDataLine& line : result.lines)
    {
        line.text = to_tstring(original_text(line));
    }
    return result;
}

static std::string trim(std::string_view input)
{
    size_t begin = 0;
    size_t end = input.size();
    while((begin < end) && ((input[begin] == ' ') || (input[begin] == '\t') || (input[begin] == '\r')))
    {
        begin++;
    }
    while((end > begin) && ((input[end - 1] == ' ') || (input[end - 1] == '\t') || (input[end - 1] == '\r')))
    {
        end--;
    }
    return std::string(input.substr(begin, end - begin));
}

// Expects lines of the form "N|text" and fills out_translations[N-1].
// Anything that is not in that form (blank lines, markdown fences, chatter) is ignored.
static size_t collect_numbered_lines(std::string_view text, std::vector<std::string>& out_translations)
{
    size_t count = 0;
    size_t line_start = 0;
    while(line_start <= text.size())
    {
        const size_t line_end = std::min(text.size(), text.find('\n', line_start));
        const std::string line = trim(text.substr(line_start, line_end - line_start));
        line_start = line_end + 1;

        if(line.empty()) continue;

        size_t digits_end = 0;
        while((digits_end < line.size()) && std::isdigit(static_cast<unsigned char>(line[digits_end])))
        {
            digits_end++;
        }
        if((digits_end == 0) || (digits_end >= line.size()) || (line[digits_end] != '|')) continue;

        const int index = std::atoi(line.substr(0, digits_end).c_str());
        if((index <= 0) || (static_cast<size_t>(index) > out_translations.size())) continue;

        out_translations[static_cast<size_t>(index) - 1] = trim(line.substr(digits_end + 1));
        count++;
    }
    return count;
}

static std::string json_string_field(const cJSON* object, const char* name)
{
    const cJSON* value = cJSON_GetObjectItemCaseSensitive(object, name);
    if(!cJSON_IsString(value) || (value->valuestring == nullptr))
    {
        return {};
    }
    return value->valuestring;
}

static std::string extract_response_text(std::string_view response_body)
{
    cJSON* root = cJSON_ParseWithLength(response_body.data(), response_body.size());
    if(root == nullptr)
    {
        LOG_WARN("AI translation: response was not valid JSON: %.400s", std::string(response_body).c_str());
        throw std::runtime_error("the API response was not valid JSON");
    }

    std::string result;
    const cJSON* choices = cJSON_GetObjectItemCaseSensitive(root, "choices");
    const cJSON* first = cJSON_IsArray(choices) ? cJSON_GetArrayItem(choices, 0) : nullptr;
    const cJSON* message = (first != nullptr) ? cJSON_GetObjectItemCaseSensitive(first, "message") : nullptr;
    if(message != nullptr)
    {
        result = json_string_field(message, "content");
    }

    if(result.empty())
    {
        const cJSON* error = cJSON_GetObjectItemCaseSensitive(root, "error");
        const std::string detail = (error != nullptr) ? json_string_field(error, "message") : std::string {};
        cJSON_Delete(root);
        if(!detail.empty())
        {
            LOG_WARN("AI translation: API error: %s", detail.c_str());
            throw std::runtime_error("the API returned an error: " + detail);
        }
        LOG_WARN("AI translation: no translation text in response: %.400s", std::string(response_body).c_str());
        throw std::runtime_error("the API response contained no translation text");
    }

    cJSON_Delete(root);
    return result;
}

static std::string post_chat_completion(const std::string& url,
                                        const std::string& api_key,
                                        const std::string& request_body,
                                        abort_callback& abort)
{
    pfc::string8 content;
    try
    {
        http_request_post_v2::ptr post;
        http_client::get()->create_request("POST")->cast(post);
        if(!post.is_valid())
        {
            throw std::runtime_error("failed to create the HTTP request");
        }

        post->add_header("User-Agent", "foo_openlyrics AI translation");
        const std::string authorisation = "Bearer " + api_key;
        post->add_header("Authorization", authorisation.c_str());
        post->set_post_data(request_body.c_str(), request_body.size(), "application/json");

        file_ptr response_file = post->run_ex(url.c_str(), abort);

        // run_ex returns normally for non-2XX replies, so the status tells us whether a
        // failure came from auth/quota (401/402/429) or from something we sent.
        http_reply::ptr reply;
        if(response_file->service_query_t(reply))
        {
            pfc::string8 status;
            reply->get_status(status);
            LOG_INFO("AI translation: HTTP status %s", status.c_str());
        }

        response_file->read_string_raw(content, abort);
        LOG_INFO("AI translation: received %u bytes", unsigned(content.get_length()));
    }
    catch(const std::exception& e)
    {
        throw std::runtime_error(std::format("the request to {} failed: {}", url, e.what()));
    }

    return std::string(content.c_str(), content.get_length());
}

LyricData translate_lyrics(const LyricData& lyrics, abort_callback& abort)
{
    const std::string api_url = preferences::translation::api_endpoint();
    const std::string api_key = preferences::translation::api_key();
    const std::string model = preferences::translation::model();
    const std::string language = preferences::translation::target_language();
    const std::string line_prefix = preferences::translation::line_prefix();

    if(api_url.empty()) throw std::runtime_error("no API endpoint is configured");
    if(api_key.empty())
    {
        throw std::runtime_error("no API key is configured: set one in Preferences -> Tools -> OpenLyrics -> "
                                 "Translation");
    }
    if(model.empty()) throw std::runtime_error("no model is configured");
    if(!lyrics.IsTimestamped()) throw std::runtime_error("only lyrics with timestamps can be translated");

    const std::vector<std::string> originals = [&lyrics]()
    {
        std::vector<std::string> out;
        out.reserve(lyrics.lines.size());
        for(const LyricDataLine& line : lyrics.lines)
        {
            out.push_back(original_text(line));
        }
        return out;
    }();

    LOG_INFO("Translating %d lyric lines into %s using %s (endpoint: %s)",
             int(originals.size()),
             language.c_str(),
             model.c_str(),
             api_url.c_str());

    std::string numbered_lines;
    for(size_t i = 0; i < originals.size(); i++)
    {
        numbered_lines += std::format("{}|{}\n", i + 1, originals[i]);
    }

    const std::string system_prompt = std::format(
        "You translate song lyrics into {}. You receive numbered lyric lines, one per line, in the form "
        "\"N|text\". Reply with exactly one line per input line, in the same order, in the form "
        "\"N|translated text\". Never merge, split, reorder or omit lines, and always keep the numbers. "
        "Do not add explanations, notes, headings or blank lines. If a line is empty or has nothing to "
        "translate, repeat it unchanged after the number.",
        language);

    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "model", model.c_str());
    cJSON_AddNumberToObject(root, "temperature", 0);
    cJSON* messages = cJSON_AddArrayToObject(root, "messages");
    cJSON* system_message = cJSON_CreateObject();
    cJSON_AddStringToObject(system_message, "role", "system");
    cJSON_AddStringToObject(system_message, "content", system_prompt.c_str());
    cJSON_AddItemToArray(messages, system_message);
    cJSON* user_message = cJSON_CreateObject();
    cJSON_AddStringToObject(user_message, "role", "user");
    cJSON_AddStringToObject(user_message, "content", numbered_lines.c_str());
    cJSON_AddItemToArray(messages, user_message);
    char* request_body = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    std::string response_body;
    try
    {
        response_body = post_chat_completion(api_url, api_key, request_body, abort);
    }
    catch(...)
    {
        cJSON_free(request_body);
        throw;
    }
    cJSON_free(request_body);

    const std::string response_text = extract_response_text(response_body);

    std::vector<std::string> translations(originals.size());
    const size_t parsed_count = collect_numbered_lines(response_text, translations);
    LOG_INFO("AI translation: parsed %d of %d lines", int(parsed_count), int(originals.size()));

    size_t missing_count = 0;
    for(size_t i = 0; i < originals.size(); i++)
    {
        if(!trim(originals[i]).empty() && translations[i].empty())
        {
            missing_count++;
        }
    }
    if(missing_count > 0)
    {
        LOG_WARN("AI translation: %d lines came back missing, response was: %.400s",
                 int(missing_count),
                 response_text.c_str());
        throw std::runtime_error(std::format("the API response was missing {} of {} lines, nothing was saved",
                                             missing_count,
                                             originals.size()));
    }

    // Marks translated lines so they are visually distinguishable from the original text. User
    // configurable, and empty by default because not every language wants one.
    const std::tstring translation_prefix = to_tstring(std::string_view { line_prefix });

    LyricData result = lyrics;
    result.lines.clear();
    result.lines.reserve(originals.size());
    for(size_t i = 0; i < originals.size(); i++)
    {
        const std::tstring original = to_tstring(originals[i]);
        const std::tstring translation = to_tstring(translations[i]);

        // Store the pair as a single compound line -- exactly the shape that reloading the saved
        // .lrc produces -- so the panel treats original+translation as one unit and highlights both
        // rows at the same time. Two separate lines with the same timestamp would instead make the
        // highlight step from the original to the translation as soon as the timestamp passes.
        // (expand_text splits the newline back into two timestamped lines when saving.)
        if(translation.empty())
        {
            // Gaps between verses: no translation, so no bare "译：" row.
            result.lines.push_back({ original, lyrics.lines[i].timestamp });
        }
        else
        {
            result.lines.push_back(
                { original + std::tstring { _T('\n') } + translation_prefix + translation, lyrics.lines[i].timestamp });
        }
    }
    return result;
}

void SpawnLyricTranslation(LyricData lyrics, metadb_handle_ptr track, metadb_v2_rec_t track_info)
{
    core_api::ensure_main_thread();

    if(lyrics.IsEmpty())
    {
        popup_message_v3::query_t query = {};
        query.title = "Translate lyrics";
        query.msg = "There are no lyrics to translate for this track.";
        query.buttons = popup_message_v3::buttonOK;
        query.icon = popup_message_v3::iconInformation;
        popup_message_v3::get()->show_query_modal(query);
        return;
    }

    if(lyrics_are_translated(lyrics))
    {
        popup_message_v3::query_t query = {};
        query.title = "Translate lyrics";
        query.msg = "These lyrics already contain a translation.\n\nTranslate them again and replace the existing "
                    "translation?";
        query.buttons = popup_message_v3::buttonYes | popup_message_v3::buttonNo;
        query.defButton = popup_message_v3::buttonNo;
        query.icon = popup_message_v3::iconWarning;
        if(popup_message_v3::get()->show_query_modal(query) != popup_message_v3::buttonYes)
        {
            return;
        }
        lyrics = lyrics_without_translation(lyrics);
    }

    const auto async_translate =
        [track, track_info, lyrics = std::move(lyrics)](threaded_process_status& /*status*/, abort_callback& abort)
    {
        try
        {
            LyricData translated = translate_lyrics(lyrics, abort);
            fb2k::inMainThread2(
                [track, track_info, translated = std::move(translated)]() mutable
                {
                    // Type::Translate saves with overwrite allowed and refreshes every visible panel,
                    // but is not treated as a user edit, so "Upload -> After manual edit" never
                    // publishes a machine translation to LRCLIB.
                    announce_lyric_update({ std::move(translated), track, track_info, LyricUpdate::Type::Translate });
                });
        }
        catch(const std::exception& e)
        {
            LOG_WARN("Failed to translate lyrics: %s", e.what());
            fb2k::inMainThread2(
                [message = std::string(e.what())]
                {
                    popup_message_v3::query_t query = {};
                    query.title = "Translate lyrics";
                    query.msg = message.c_str();
                    query.buttons = popup_message_v3::buttonOK;
                    query.icon = popup_message_v3::iconWarning;
                    popup_message_v3::get()->show_query_modal(query);
                });
        }
    };

    threaded_process::g_run_modeless(threaded_process_callback_lambda::create(async_translate),
                                     threaded_process::flag_show_delayed | threaded_process::flag_show_abort,
                                     core_api::get_main_window(),
                                     "Translating lyrics...");
}
