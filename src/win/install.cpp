/**
 * install.cpp - Win64 installer implementation
 * Summary: Native installer logic for the inffusion runtime on Windows.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
 */

#define _WIN32_WINNT 0x0601

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <urlmon.h>

#define KC_INSTALLER_TITLE "inffusion installer"
#define KC_INSTALL_ROOT_DEFAULT "C:\\Program Files\\KaisarCode"
#define KC_REPO_ROOT "https://raw.githubusercontent.com/kaisarcode/inffusion.cpp/v1.0.1"
#define KC_MAX_FILES 16

struct kc_file_entry {
    const char *url;
    const char *target;
};

struct kc_config {
    int yes_mode;
    int custom_root;
    char install_root[MAX_PATH];
};

static const struct kc_file_entry KC_FILES[] = {
    { KC_REPO_ROOT "/bin/win64/inffusion.exe", "bin\\inffusion.exe" },
    { KC_REPO_ROOT "/lib/obj/stable-diffusion.cpp/win64/libstable-diffusion.dll", "win64\\libstable-diffusion.dll" },
    { KC_REPO_ROOT "/lib/obj/ggml/win64/ggml-base.dll", "win64\\ggml-base.dll" },
    { KC_REPO_ROOT "/lib/obj/ggml/win64/ggml-cpu.dll", "win64\\ggml-cpu.dll" },
    { KC_REPO_ROOT "/lib/obj/ggml/win64/ggml.dll", "win64\\ggml.dll" },
    { KC_REPO_ROOT "/lib/obj/ggml/win64/libgcc_s_seh-1.dll", "win64\\libgcc_s_seh-1.dll" },
    { KC_REPO_ROOT "/lib/obj/ggml/win64/libgomp-1.dll", "win64\\libgomp-1.dll" },
    { KC_REPO_ROOT "/lib/obj/ggml/win64/libstdc++-6.dll", "win64\\libstdc++-6.dll" },
    { KC_REPO_ROOT "/lib/obj/ggml/win64/libwinpthread-1.dll", "win64\\libwinpthread-1.dll" }
};

/**
 * Shows one message dialog when interactive mode is enabled.
 * @param kind Message box flags.
 * @param title Dialog title.
 * @param message Dialog body.
 * @param yes_mode Whether dialogs should be skipped.
 * @return Dialog result or IDYES when skipped.
 */
static int kc_message_box(UINT kind, const char *title, const char *message, int yes_mode) {
    if (yes_mode) {
        return IDYES;
    }
    return MessageBoxA(NULL, message, title, kind);
}

/**
 * Shows one error dialog.
 * @param message Error text.
 * @param yes_mode Whether dialogs should be skipped.
 * @return Does not return a value.
 */
static void kc_fail_box(const char *message, int yes_mode) {
    kc_message_box(MB_ICONERROR | MB_OK, KC_INSTALLER_TITLE, message, yes_mode);
}

/**
 * Shows one success dialog.
 * @param message Success text.
 * @param yes_mode Whether dialogs should be skipped.
 * @return Does not return a value.
 */
static void kc_success_box(const char *message, int yes_mode) {
    kc_message_box(MB_ICONINFORMATION | MB_OK, KC_INSTALLER_TITLE, message, yes_mode);
}

/**
 * Checks whether the current process already has administrator rights.
 * @return 1 when elevated, otherwise 0.
 */
static int kc_is_admin(void) {
    BOOL is_member = FALSE;
    SID_IDENTIFIER_AUTHORITY nt_authority = SECURITY_NT_AUTHORITY;
    PSID admin_group = NULL;

    if (!AllocateAndInitializeSid(&nt_authority, 2,
        SECURITY_BUILTIN_DOMAIN_RID,
        DOMAIN_ALIAS_RID_ADMINS,
        0, 0, 0, 0, 0, 0,
        &admin_group)) {
        return 0;
    }
    if (!CheckTokenMembership(NULL, admin_group, &is_member)) {
        is_member = FALSE;
    }
    FreeSid(admin_group);
    return is_member ? 1 : 0;
}

/**
 * Relaunches the installer requesting administrator privileges.
 * @param cmdline Command-line arguments to preserve.
 * @param yes_mode Whether dialogs should be skipped.
 * @return 0 when the elevated process was started, otherwise 1.
 */
static int kc_relaunch_as_admin(const char *cmdline, int yes_mode) {
    char exe_path[MAX_PATH];
    SHELLEXECUTEINFOA sei;

    if (GetModuleFileNameA(NULL, exe_path, sizeof(exe_path)) == 0) {
        kc_fail_box("Unable to resolve installer path.", yes_mode);
        return 1;
    }

    memset(&sei, 0, sizeof(sei));
    sei.cbSize = sizeof(sei);
    sei.lpVerb = "runas";
    sei.lpFile = exe_path;
    sei.lpParameters = cmdline;
    sei.nShow = SW_SHOWNORMAL;

    if (!ShellExecuteExA(&sei)) {
        kc_fail_box("Administrator privileges are required to install this app.", yes_mode);
        return 1;
    }

    return 0;
}

/**
 * Copies one string into a fixed buffer.
 * @param dst Destination buffer.
 * @param dst_size Destination buffer size.
 * @param src Source text.
 * @return 0 on success, otherwise 1.
 */
static int kc_copy_text(char *dst, size_t dst_size, const char *src) {
    if (!dst || !src || strlen(src) + 1u > dst_size) {
        return 1;
    }
    strcpy(dst, src);
    return 0;
}

/**
 * Creates every directory component required by one path.
 * @param path Directory path.
 * @return 0 on success, otherwise 1.
 */
static int kc_ensure_directory(const char *path) {
    char temp[MAX_PATH];
    char *cursor;

    if (kc_copy_text(temp, sizeof(temp), path) != 0) {
        return 1;
    }
    for (cursor = temp + 3; *cursor != '\0'; cursor++) {
        if (*cursor == '\\' || *cursor == '/') {
            char saved = *cursor;
            *cursor = '\0';
            if (CreateDirectoryA(temp, NULL) == 0 && GetLastError() != ERROR_ALREADY_EXISTS) {
                return 1;
            }
            *cursor = saved;
        }
    }
    if (CreateDirectoryA(temp, NULL) == 0 && GetLastError() != ERROR_ALREADY_EXISTS) {
        return 1;
    }
    return 0;
}

/**
 * Downloads one remote file to disk.
 * @param url Remote URL.
 * @param path Destination path.
 * @return 0 on success, otherwise 1.
 */
static int kc_download_file(const char *url, const char *path) {
    return URLDownloadToFileA(NULL, url, path, 0, NULL) == S_OK ? 0 : 1;
}

/**
 * Appends one directory entry to the system PATH when missing.
 * @param install_root Root install directory.
 * @param entry Relative path entry.
 * @return 0 on success, otherwise 1.
 */
static int kc_append_path_entry(const char *install_root, const char *entry) {
    HKEY key;
    DWORD type = 0;
    DWORD size = 0;
    char path_value[8192];
    char full_entry[MAX_PATH];
    LONG status;

    if (PathCombineA(full_entry, install_root, entry) == NULL) {
        return 1;
    }
    status = RegOpenKeyExA(HKEY_LOCAL_MACHINE,
        "SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Environment",
        0,
        KEY_QUERY_VALUE | KEY_SET_VALUE,
        &key);
    if (status != ERROR_SUCCESS) {
        return 1;
    }

    size = sizeof(path_value);
    status = RegQueryValueExA(key, "Path", NULL, &type, (LPBYTE)path_value, &size);
    if (status != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ)) {
        path_value[0] = '\0';
    } else {
        path_value[sizeof(path_value) - 1] = '\0';
    }

    if (StrStrIA(path_value, full_entry) == NULL) {
        size_t length = strlen(path_value);
        size_t needed = strlen(full_entry) + (length ? 1u : 0u) + 1u;
        if (length + needed >= sizeof(path_value)) {
            RegCloseKey(key);
            return 1;
        }
        if (length > 0) {
            path_value[length++] = ';';
            path_value[length] = '\0';
        }
        strcat(path_value, full_entry);
        status = RegSetValueExA(key, "Path", 0, REG_EXPAND_SZ,
            (const BYTE *)path_value,
            (DWORD)(strlen(path_value) + 1u));
        if (status != ERROR_SUCCESS) {
            RegCloseKey(key);
            return 1;
        }
        SendMessageTimeoutA(HWND_BROADCAST, WM_SETTINGCHANGE, 0,
            (LPARAM)"Environment", SMTO_ABORTIFHUNG, 5000, NULL);
    }

    RegCloseKey(key);
    return 0;
}

/**
 * Parses installer arguments.
 * @param cmdline Raw command line.
 * @param config Output config.
 * @return 0 on success, otherwise 1.
 */
static int kc_parse_args(const char *cmdline, struct kc_config *config) {
    int argc;
    int i;
    LPWSTR *argv_w;

    memset(config, 0, sizeof(*config));
    if (kc_copy_text(config->install_root, sizeof(config->install_root), KC_INSTALL_ROOT_DEFAULT) != 0) {
        return 1;
    }

    argv_w = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv_w) {
        return 1;
    }
    for (i = 1; i < argc; i++) {
        char arg[MAX_PATH];
        if (WideCharToMultiByte(CP_UTF8, 0, argv_w[i], -1, arg, sizeof(arg), NULL, NULL) == 0) {
            LocalFree(argv_w);
            return 1;
        }
        if (strcmp(arg, "--yes") == 0) {
            config->yes_mode = 1;
            continue;
        }
        if (strcmp(arg, "--help") == 0) {
            kc_message_box(MB_ICONINFORMATION | MB_OK, KC_INSTALLER_TITLE,
                "Usage:\n  install.exe [--yes] [--root <path>]\n", config->yes_mode);
            LocalFree(argv_w);
            exit(0);
        }
        if (strcmp(arg, "--root") == 0) {
            char root_path[MAX_PATH];
            if (i + 1 >= argc ||
                WideCharToMultiByte(CP_UTF8, 0, argv_w[++i], -1, root_path, sizeof(root_path), NULL, NULL) == 0 ||
                kc_copy_text(config->install_root, sizeof(config->install_root), root_path) != 0) {
                LocalFree(argv_w);
                return 1;
            }
            config->custom_root = 1;
            continue;
        }
        LocalFree(argv_w);
        return 1;
    }
    LocalFree(argv_w);
    (void)cmdline;
    return 0;
}

/**
 * Asks for installation confirmation.
 * @param config Active config.
 * @return 0 on approval, otherwise 1.
 */
static int kc_confirm_install(const struct kc_config *config) {
    char message[1024];

    snprintf(message, sizeof(message),
        "Install inffusion to:\n\n%s\n\nThis will place the executable in bin and the runtime DLLs in win64.",
        config->install_root);
    return kc_message_box(MB_ICONQUESTION | MB_YESNO, KC_INSTALLER_TITLE, message, config->yes_mode) == IDYES ? 0 : 1;
}

/**
 * Installs every runtime file.
 * @param config Active config.
 * @return 0 on success, otherwise 1.
 */
static int kc_install_files(const struct kc_config *config) {
    char target_path[MAX_PATH];
    char target_dir[MAX_PATH];
    char temp_path[MAX_PATH];
    int i;

    for (i = 0; i < (int)(sizeof(KC_FILES) / sizeof(KC_FILES[0])); i++) {
        if (PathCombineA(target_path, config->install_root, KC_FILES[i].target) == NULL) {
            return 1;
        }
        if (kc_copy_text(target_dir, sizeof(target_dir), target_path) != 0) {
            return 1;
        }
        PathRemoveFileSpecA(target_dir);
        if (kc_ensure_directory(target_dir) != 0) {
            return 1;
        }
        if (GetTempFileNameA(target_dir, "kci", 0, temp_path) == 0) {
            return 1;
        }
        if (kc_download_file(KC_FILES[i].url, temp_path) != 0) {
            DeleteFileA(temp_path);
            return 1;
        }
        if (MoveFileExA(temp_path, target_path, MOVEFILE_REPLACE_EXISTING) == 0) {
            DeleteFileA(temp_path);
            return 1;
        }
    }
    return 0;
}

/**
 * Updates PATH entries used by inffusion.
 * @param config Active config.
 * @return 0 on success, otherwise 1.
 */
static int kc_install_path_entries(const struct kc_config *config) {
    if (kc_append_path_entry(config->install_root, "bin") != 0) {
        return 1;
    }
    if (kc_append_path_entry(config->install_root, "win64") != 0) {
        return 1;
    }
    return 0;
}

/**
 * Runs the installer entry point.
 * @param instance Current process instance handle.
 * @param prev Unused previous instance handle.
 * @param cmdline Raw command line string.
 * @param show Initial show state.
 * @return Process exit status.
 */
int WINAPI WinMain(HINSTANCE instance, HINSTANCE prev, LPSTR cmdline, int show) {
    struct kc_config config;

    (void)instance;
    (void)prev;
    (void)show;

    if (kc_parse_args(cmdline, &config) != 0) {
        kc_fail_box("Invalid installer arguments.", 0);
        return 1;
    }
    if (!config.custom_root && !kc_is_admin()) {
        return kc_relaunch_as_admin(cmdline, config.yes_mode);
    }
    if (kc_confirm_install(&config) != 0) {
        return 1;
    }
    if (kc_install_files(&config) != 0) {
        kc_fail_box("Unable to install application files.", config.yes_mode);
        return 1;
    }
    if (!config.custom_root && kc_install_path_entries(&config) != 0) {
        kc_fail_box("Unable to update PATH.", config.yes_mode);
        return 1;
    }
    kc_success_box("Application installed successfully.", config.yes_mode);
    return 0;
}
