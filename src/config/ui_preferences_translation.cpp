#include "stdafx.h"

#pragma warning(push, 0)
#include "foobar2000/SDK/coreDarkMode.h"
#include "foobar2000/helpers/atl-misc.h"
#include "resource.h"
#pragma warning(pop)

#include "config/config_auto.h"
#include "logging.h"
#include "preferences.h"

#include <dpapi.h>
#include <wincrypt.h>
#pragma comment(lib, "crypt32.lib")

// clang-format off: GUIDs should be one line
extern const GUID GUID_PREFERENCES_PAGE_ROOT;

static const GUID GUID_PREFERENCES_PAGE_TRANSLATION = { 0x4c8f2b19, 0x7d3a, 0x45e6, { 0xb2, 0x84, 0x19, 0xcf, 0x63, 0xa, 0xd7, 0x25 } };

static const GUID GUID_CFG_TRANSLATE_ENDPOINT = { 0x2f6c1d80, 0x5b47, 0x4e12, { 0x8d, 0x63, 0x1a, 0x94, 0x7f, 0x20, 0xc5, 0x48 } };
static const GUID GUID_CFG_TRANSLATE_MODEL = { 0x8d13f5a1, 0x3e6b, 0x49d0, { 0xa7, 0x52, 0x6c, 0x18, 0x3b, 0xe4, 0x90, 0x27 } };
static const GUID GUID_CFG_TRANSLATE_LANGUAGE = { 0x31e7c9d5, 0x91a4, 0x4c3f, { 0xbe, 0x70, 0x5d, 0x82, 0x19, 0x6a, 0xf3, 0x54 } };
static const GUID GUID_CFG_TRANSLATE_PREFIX = { 0x9a4d6e02, 0xc7f8, 0x4b15, { 0x83, 0x2a, 0xd7, 0x60, 0x1f, 0x9e, 0x45, 0xb8 } };
static const GUID GUID_CFG_TRANSLATE_KEY = { 0x5c40b8e2, 0xa1d9, 0x4f76, { 0x95, 0x3c, 0x70, 0xbe, 0x28, 0xd4, 0x16, 0x8a } };
// clang-format on

static cfg_auto_string cfg_translate_endpoint(GUID_CFG_TRANSLATE_ENDPOINT,
                                              IDC_TRANSLATE_API_ENDPOINT,
                                              "https://api.deepseek.com/v1/chat/completions");
static cfg_auto_string cfg_translate_model(GUID_CFG_TRANSLATE_MODEL, IDC_TRANSLATE_MODEL, "deepseek-chat");
static cfg_auto_string cfg_translate_language(GUID_CFG_TRANSLATE_LANGUAGE,
                                              IDC_TRANSLATE_LANGUAGE,
                                              "Simplified Chinese (\u7b80\u4f53\u4e2d\u6587)");
static cfg_auto_string cfg_translate_prefix(GUID_CFG_TRANSLATE_PREFIX, IDC_TRANSLATE_PREFIX, "");

// The API key is deliberately not a plain config string: what lands in the config database is a
// DPAPI blob, so it can only be read back by the Windows user that saved it, and never by simply
// opening config.sqlite.
static cfg_string cfg_translate_key(GUID_CFG_TRANSLATE_KEY, "");

static cfg_auto_property* g_translation_auto_properties[] = {
    &cfg_translate_endpoint,
    &cfg_translate_model,
    &cfg_translate_language,
    &cfg_translate_prefix,
};

static std::string encrypt_api_key(std::string_view plain)
{
    if(plain.empty()) return {};
    DATA_BLOB input = {};
    input.pbData = reinterpret_cast<BYTE*>(const_cast<char*>(plain.data()));
    input.cbData = static_cast<DWORD>(plain.size());

    DATA_BLOB encrypted = {};
    if(!CryptProtectData(&input, L"OpenLyrics lyrics translation API key", nullptr, nullptr, nullptr, 0, &encrypted))
    {
        LOG_ERROR("Failed to encrypt the translation API key: 0x%08x", GetLastError());
        return {};
    }

    DWORD base64_length = 0;
    std::string result;
    if(CryptBinaryToStringA(encrypted.pbData,
                            encrypted.cbData,
                            CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF,
                            nullptr,
                            &base64_length))
    {
        result.resize(base64_length);
        if(!CryptBinaryToStringA(encrypted.pbData,
                                 encrypted.cbData,
                                 CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF,
                                 result.data(),
                                 &base64_length))
        {
            LOG_ERROR("Failed to base64-encode the translation API key: 0x%08x", GetLastError());
            result.clear();
        }
        result.resize(base64_length);
        while(!result.empty() && (result.back() == '\0'))
            result.pop_back();
    }
    LocalFree(encrypted.pbData);
    return result;
}

static std::string decrypt_api_key(std::string_view stored)
{
    if(stored.empty()) return {};

    DWORD blob_length = 0;
    if(!CryptStringToBinaryA(stored.data(),
                             static_cast<DWORD>(stored.size()),
                             CRYPT_STRING_BASE64,
                             nullptr,
                             &blob_length,
                             nullptr,
                             nullptr))
    {
        LOG_WARN("Saved translation API key is not valid base64");
        return {};
    }

    std::vector<BYTE> blob(blob_length);
    if(!CryptStringToBinaryA(stored.data(),
                             static_cast<DWORD>(stored.size()),
                             CRYPT_STRING_BASE64,
                             blob.data(),
                             &blob_length,
                             nullptr,
                             nullptr))
    {
        LOG_WARN("Saved translation API key could not be decoded");
        return {};
    }

    DATA_BLOB input = {};
    input.pbData = blob.data();
    input.cbData = blob_length;

    DATA_BLOB plain = {};
    if(!CryptUnprotectData(&input, nullptr, nullptr, nullptr, nullptr, 0, &plain))
    {
        LOG_WARN("Saved translation API key could not be decrypted (saved by a different Windows user?): 0x%08x",
                 GetLastError());
        return {};
    }

    std::string result(reinterpret_cast<const char*>(plain.pbData), plain.cbData);
    LocalFree(plain.pbData);
    return result;
}

std::string preferences::translation::api_endpoint()
{
    return std::string(cfg_translate_endpoint.get_ptr());
}

std::string preferences::translation::model()
{
    return std::string(cfg_translate_model.get_ptr());
}

std::string preferences::translation::target_language()
{
    return std::string(cfg_translate_language.get_ptr());
}

std::string preferences::translation::line_prefix()
{
    return std::string(cfg_translate_prefix.get_ptr());
}

std::string preferences::translation::api_key()
{
    return decrypt_api_key(std::string_view { cfg_translate_key.get_ptr() });
}

bool preferences::translation::has_api_key()
{
    return cfg_translate_key.get_ptr()[0] != '\0';
}

class PreferencesTranslation : public CDialogImpl<PreferencesTranslation>, public auto_preferences_page_instance
{
public:
    PreferencesTranslation(preferences_page_callback::ptr callback)
        : auto_preferences_page_instance(callback, g_translation_auto_properties)
    {
    }

    // Dialog resource ID - Required by WTL/Create()
    enum
    {
        IDD = IDD_PREFERENCES_TRANSLATION
    };

    void apply() override;
    void reset() override;
    bool has_changed() override;

    BEGIN_MSG_MAP_EX(PreferencesTranslation)
    MSG_WM_INITDIALOG(OnInitDialog)
    COMMAND_HANDLER_EX(IDC_TRANSLATE_API_ENDPOINT, EN_CHANGE, OnUIChange)
    COMMAND_HANDLER_EX(IDC_TRANSLATE_MODEL, EN_CHANGE, OnUIChange)
    COMMAND_HANDLER_EX(IDC_TRANSLATE_LANGUAGE, EN_CHANGE, OnUIChange)
    COMMAND_HANDLER_EX(IDC_TRANSLATE_PREFIX, EN_CHANGE, OnUIChange)
    COMMAND_HANDLER_EX(IDC_TRANSLATE_API_KEY, EN_CHANGE, OnKeyChange)
    END_MSG_MAP()

private:
    BOOL OnInitDialog(CWindow, LPARAM);
    void OnUIChange(UINT, int, CWindow);
    void OnKeyChange(UINT, int, CWindow);
    void UpdateKeyStatus();

    bool m_key_edited = false;
    fb2k::CCoreDarkModeHooks m_dark;
};

void PreferencesTranslation::apply()
{
    auto_preferences_page_instance::apply();

    if(m_key_edited)
    {
        const int length = ::GetWindowTextLength(::GetDlgItem(m_hWnd, IDC_TRANSLATE_API_KEY));
        if(length > 0)
        {
            std::vector<TCHAR> buffer(static_cast<size_t>(length) + 1);
            ::GetDlgItemText(m_hWnd, IDC_TRANSLATE_API_KEY, buffer.data(), length + 1);
            const std::string typed = from_tstring(std::tstring_view { buffer.data(), static_cast<size_t>(length) });
            const std::string encrypted = encrypt_api_key(typed);
            if(!encrypted.empty())
            {
                cfg_translate_key.set_string(pfc::string8(encrypted.c_str(), encrypted.length()));
            }
        }

        // Never leave the typed key sitting in the dialog.
        SetDlgItemText(IDC_TRANSLATE_API_KEY, _T(""));
        m_key_edited = false;
    }

    UpdateKeyStatus();
    on_ui_interaction();
}

void PreferencesTranslation::reset()
{
    auto_preferences_page_instance::reset();
    SetDlgItemText(IDC_TRANSLATE_API_KEY, _T(""));
    m_key_edited = false;
    UpdateKeyStatus();
    on_ui_interaction();
}

bool PreferencesTranslation::has_changed()
{
    return auto_preferences_page_instance::has_changed() || m_key_edited;
}

BOOL PreferencesTranslation::OnInitDialog(CWindow, LPARAM)
{
    m_dark.AddDialogWithControls(m_hWnd);

    init_auto_preferences();
    UpdateKeyStatus();
    return FALSE;
}

void PreferencesTranslation::OnUIChange(UINT, int, CWindow)
{
    on_ui_interaction();
}

void PreferencesTranslation::OnKeyChange(UINT, int, CWindow)
{
    m_key_edited = (::GetWindowTextLength(::GetDlgItem(m_hWnd, IDC_TRANSLATE_API_KEY)) > 0);
    on_ui_interaction();
}

void PreferencesTranslation::UpdateKeyStatus()
{
    const bool has_key = preferences::translation::has_api_key();
    SetDlgItemText(IDC_TRANSLATE_KEY_STATUS,
                   has_key
                       ? _T("A key is saved, encrypted for this Windows user. Leave the field above blank to keep it.")
                       : _T("No key saved yet."));
}

class PreferencesTranslationImpl : public preferences_page_impl<PreferencesTranslation>
{
public:
    const char* get_name() final
    {
        return "Translation";
    }
    GUID get_guid() final
    {
        return GUID_PREFERENCES_PAGE_TRANSLATION;
    }
    GUID get_parent_guid() final
    {
        return GUID_PREFERENCES_PAGE_ROOT;
    }
};
static preferences_page_factory_t<PreferencesTranslationImpl> g_preferences_page_translation_factory;
