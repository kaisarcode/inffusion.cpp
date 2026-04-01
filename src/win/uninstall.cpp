/**
 * uninstall.cpp - Win64 uninstaller implementation
 * Summary: Native uninstaller logic for the inffusion runtime on Windows.
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

#define KC_UNINSTALLER_TITLE "inffusion uninstaller"
#define KC_INSTALL_ROOT_DEFAULT "C:\\Program Files\\KaisarCode"

struct kc_config {
    int yes_mode;
    int remove_deps;
    int remove_deps_set;
    int custom_root;
    char install_root[MAX_PATH];
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
    kc_message_box(MB_ICONERROR | MB_OK, KC_UNINSTALLER_TITLE, message, yes_mode);
}

/**
 * Shows one success dialog.
 * @param message Success text.
 * @param yes_mode Whether dialogs should be skipped.
 * @return Does not return a value.
 */
static void kc_success_box(const char *message, int yes_mode) {
    kc_message_box(MB_ICONINFORMATION | MB_OK, KC_UNINSTALLER_TITLE, message, yes_mode);
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
 * Relaunches the uninstaller requesting administrator privileges.
 * @param cmdline Command-line arguments to preserve.
 * @param yes_mode Whether dialogs should be skipped.
 * @return 0 when the elevated process was started, otherwise 1.
 */
static int kc_relaunch_as_admin(const char *cmdline, int yes_mode) {
    char exe_path[MAX_PATH];
    SHELLEXECUTEINFOA sei;

    if (GetModuleFileNameA(NULL, exe_path, sizeof(exe_path)) == 0) {
        kc_fail_box("Unable to resolve uninstaller path.", yes_mode);
        return 1;
    }

    memset(&sei, 0, sizeof(sei));
    sei.cbSize = sizeof(sei);
    sei.lpVerb = "runas";
    sei.lpFile = exe_path;
    sei.lpParameters = cmdline;
    sei.nShow = SW_SHOWNORMAL;

    if (!ShellExecuteExA(&sei)) {
        kc_fail_box("Administrator privileges are required to remove this app.", yes_mode);
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
 * Parses uninstaller arguments.
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
        if (strcmp(arg, "--deps") == 0) {
            config->remove_deps = 1;
            config->remove_deps_set = 1;
            continue;
        }
        if (strcmp(arg, "--help") == 0) {
            kc_message_box(MB_ICONINFORMATION | MB_OK, KC_UNINSTALLER_TITLE,
                "Usage:\n  uninstall.exe [--yes] [--deps] [--root <path>]\n", config->yes_mode);
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
 * Removes one file when it exists.
 * @param path Target path.
 * @return 0 on success, otherwise 1.
 */
static int kc_remove_file_if_exists(const char *path) {
    DWORD attr = GetFileAttributesA(path);
    if (attr == INVALID_FILE_ATTRIBUTES) {
        return 0;
    }
    if ((attr & FILE_ATTRIBUTE_DIRECTORY) != 0) {
        return 1;
    }
    return DeleteFileA(path) ? 0 : 1;
}

/**
 * Removes one directory tree from the deepest children up to the root.
 * @param path Directory path.
 * @return 0 on success, otherwise 1.
 */
static int kc_remove_tree(const char *path) {
    char pattern[MAX_PATH];
    WIN32_FIND_DATAA data;
    HANDLE handle;

    if (PathCombineA(pattern, path, "*") == NULL) {
        return 1;
    }
    handle = FindFirstFileA(pattern, &data);
    if (handle == INVALID_HANDLE_VALUE) {
        return RemoveDirectoryA(path) || GetLastError() == ERROR_PATH_NOT_FOUND ? 0 : 1;
    }
    do {
        char child[MAX_PATH];
        if (strcmp(data.cFileName, ".") == 0 || strcmp(data.cFileName, "..") == 0) {
            continue;
        }
        if (PathCombineA(child, path, data.cFileName) == NULL) {
            FindClose(handle);
            return 1;
        }
        if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
            if (kc_remove_tree(child) != 0) {
                FindClose(handle);
                return 1;
            }
        } else {
            if (!DeleteFileA(child)) {
                FindClose(handle);
                return 1;
            }
        }
    } while (FindNextFileA(handle, &data));
    FindClose(handle);
    return RemoveDirectoryA(path) || GetLastError() == ERROR_PATH_NOT_FOUND ? 0 : 1;
}

/**
 * Decides whether shared runtime DLLs should also be removed.
 * @param config Active config.
 * @return 0 on success, otherwise 1.
 */
static int kc_choose_remove_deps(struct kc_config *config) {
    char message[1024];
    int result;

    if (config->remove_deps_set) {
        return 0;
    }
    snprintf(message, sizeof(message),
        "Remove shared runtime DLLs from:\n\n%s\\win64\n\nYes: remove app and dependencies\nNo: remove only the app\nCancel: abort",
        config->install_root);
    result = kc_message_box(MB_ICONQUESTION | MB_YESNOCANCEL, KC_UNINSTALLER_TITLE, message, config->yes_mode);
    if (result == IDCANCEL) {
        return 1;
    }
    config->remove_deps = result == IDYES ? 1 : 0;
    return 0;
}

/**
 * Asks for uninstall confirmation.
 * @param config Active config.
 * @return 0 on approval, otherwise 1.
 */
static int kc_confirm_uninstall(const struct kc_config *config) {
    char message[1024];

    snprintf(message, sizeof(message),
        "Remove inffusion from:\n\n%s\n\nRemove shared runtime DLLs: %s",
        config->install_root,
        config->remove_deps ? "yes" : "no");
    return kc_message_box(MB_ICONQUESTION | MB_YESNO, KC_UNINSTALLER_TITLE, message, config->yes_mode) == IDYES ? 0 : 1;
}

/**
 * Removes installed application files.
 * @param config Active config.
 * @return 0 on success, otherwise 1.
 */
static int kc_remove_app(const struct kc_config *config) {
    char bin_path[MAX_PATH];
    char bin_dir[MAX_PATH];

    if (PathCombineA(bin_path, config->install_root, "bin\\inffusion.exe") == NULL) {
        return 1;
    }
    if (kc_remove_file_if_exists(bin_path) != 0) {
        return 1;
    }
    if (PathCombineA(bin_dir, config->install_root, "bin") == NULL) {
        return 1;
    }
    RemoveDirectoryA(bin_dir);
    return 0;
}

/**
 * Removes installed shared runtime files.
 * @param config Active config.
 * @return 0 on success, otherwise 1.
 */
static int kc_remove_deps(const struct kc_config *config) {
    char dep_dir[MAX_PATH];

    if (PathCombineA(dep_dir, config->install_root, "win64") == NULL) {
        return 1;
    }
    return kc_remove_tree(dep_dir);
}

/**
 * Runs the uninstaller entry point.
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
        kc_fail_box("Invalid uninstaller arguments.", 0);
        return 1;
    }
    if (!config.custom_root && !kc_is_admin()) {
        return kc_relaunch_as_admin(cmdline, config.yes_mode);
    }
    if (kc_choose_remove_deps(&config) != 0) {
        return 1;
    }
    if (kc_confirm_uninstall(&config) != 0) {
        return 1;
    }
    if (kc_remove_app(&config) != 0) {
        kc_fail_box("Unable to remove application files.", config.yes_mode);
        return 1;
    }
    if (config.remove_deps && kc_remove_deps(&config) != 0) {
        kc_fail_box("Unable to remove runtime dependencies.", config.yes_mode);
        return 1;
    }
    kc_success_box("Application removed successfully.", config.yes_mode);
    return 0;
}
