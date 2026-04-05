/**
 * inffusion - Raw stable-diffusion.cpp entry point
 * Summary: Implements the compact infer operation for inffusion.
 *
 * Author:  KaisarCode
 * Website: https://kaisarcode.com
 * License: https://www.gnu.org/licenses/gpl-3.0.html
 */

#define _POSIX_C_SOURCE 200809L

#include "pal.h"

#include <errno.h>
#include <ggml-backend.h>
#include <limits.h>
#include <stable-diffusion.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <thread>
#include <time.h>
#include <vector>

#ifdef _WIN32
#include <direct.h>
#include <process.h>
#else
#include <dlfcn.h>
#include <unistd.h>
#endif

#define STB_IMAGE_IMPLEMENTATION
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#include <thirdparty/stb_image.h>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <thirdparty/stb_image_write.h>
#pragma GCC diagnostic pop

#define INFUSSION_DEFAULT_WIDTH 1024
#define INFUSSION_DEFAULT_HEIGHT 1024
#define INFUSSION_DEFAULT_STEPS 20
#define INFUSSION_DEFAULT_CFG 7.0f
#define INFUSSION_DEFAULT_STRENGTH 0.75f
#define INFUSSION_DEFAULT_SEED -1
#define INFUSSION_DEFAULT_METHOD "euler_a"
#define INFUSSION_DEFAULT_GUIDANCE 3.5f
#define INFUSSION_DEFAULT_CLIP_SKIP 1
#define INFUSSION_VERSION "1.0.1"
typedef enum {
    INFUSSION_COMMAND_NONE = 0,
    INFUSSION_COMMAND_INFER
} inffusion_command;

typedef enum {
    INFUSSION_TYPE_TEXT = 0,
    INFUSSION_TYPE_IMAGE
} inffusion_type;

typedef struct {
    inffusion_command command;
    inffusion_type type;
    const char *model_path;
    const char *negative_prompt;
    std::vector<const char *> ref_paths;
    const char *output_path;
    const char *vae_path;
    const char *sample_method;
    std::vector<const char *> lora_paths;
    std::vector<float> lora_scales;
    int width;
    int height;
    int steps;
    int threads;
    int clip_skip;
    float cfg_scale;
    float strength;
    int64_t seed;
    float guidance;
    bool preview;
    bool vae_tiling;
    bool flash_attn;
    bool cpu_offload;
    bool clip_on_cpu;
    bool vae_on_cpu;
} inffusion_config;

static const char *g_preview_output_path = NULL;
static bool g_preview_write_failed = false;
static int g_last_progress_step = -1;

/**
 * Loads dynamic ggml backends from the same directory as libggml.
 * @return void
 */
static void inffusion_load_backends(void) {
#ifdef _WIN32
    ggml_backend_load_all();
#else
    Dl_info info;
    char path[PATH_MAX];
    char *slash = NULL;

    memset(&info, 0, sizeof(info));
    memset(path, 0, sizeof(path));
    if (dladdr((void *)ggml_backend_load_all_from_path, &info) == 0 || info.dli_fname == NULL) {
        ggml_backend_load_all();
        return;
    }
    if (snprintf(path, sizeof(path), "%s", info.dli_fname) < 0 || path[0] == '\0') {
        ggml_backend_load_all();
        return;
    }
    slash = strrchr(path, '/');
    if (slash == NULL) {
        ggml_backend_load_all();
        return;
    }
    *slash = '\0';
    ggml_backend_load_all_from_path(path);
#endif
}

/**
 * Resolves the effective model path from CLI or environment.
 * @param config Mutable runtime configuration.
 * @return void
 */
static void inffusion_resolve_model_path(inffusion_config *config) {
    const char *env_model = NULL;

    if (!config || (config->model_path && config->model_path[0])) {
        return;
    }
    env_model = getenv("INFFUSION_MODEL");
    if (env_model && env_model[0]) {
        config->model_path = env_model;
    }
}

/**
 * Prints compact command help.
 * @return void
 */
static void inffusion_help(void) {
    printf("Usage:\n");
    printf("  inffusion infer [options]\n\n");
    printf("Commands:\n");
    printf("  infer                Generate one image from stdin prompt\n\n");
    printf("Shared options:\n");
    printf("  --model <path>       Path to the Stable Diffusion model\n");
    printf("  --type <type>        Input type: text or image (default: text)\n");
    printf("  --output <path>      Output image path (default: auto in current directory)\n");
    printf("  --negative <text>    Negative prompt\n");
    printf("  --ref <path>         Reference image path (repeatable)\n");
    printf("  --width <int>        Output width (default: 1024)\n");
    printf("  --height <int>       Output height (default: 1024)\n");
    printf("  --steps <int>        Inference steps (default: 20)\n");
    printf("  --threads <int>      CPU thread count (default: auto)\n");
    printf("  --cfg <float>        CFG scale (default: 7.0)\n");
    printf("  --strength <float>   Img2img strength (default: 0.75)\n");
    printf("  --seed <int>         RNG seed (default: -1)\n");
    printf("  --method <name>      Sample method (default: euler_a)\n");
    printf("  --vae <path>         External VAE model path\n");
    printf("  --clip-skip <int>    CLIP skip count (default: 1)\n");
    printf("  --lora <path>        LoRA adapter path\n");
    printf("  --lora-scale <f>     Scale for the previous LoRA adapter\n");
    printf("  --guidance <float>   Distilled guidance scale (default: 3.5)\n");
    printf("  --preview            Update the output image during generation\n");
    printf("  --vae-tiling         Enable VAE tiling\n");
    printf("  --flash-attn         Enable Flash Attention\n");
    printf("  --cpu-offload        Offload weights to RAM\n");
    printf("  --clip-on-cpu        Keep CLIP on CPU\n");
    printf("  --vae-on-cpu         Keep VAE on CPU\n");
    printf("  --version, -v        Show version\n");
    printf("  --help               Show help\n");
}

/**
 * Prints the binary version.
 * @return void
 */
static void inffusion_version(void) {
    printf("inffusion %s\n", INFUSSION_VERSION);
}

/**
 * Prints one CLI error.
 * @param message Error text.
 * @return int Always returns 1.
 */
static int inffusion_fail(const char *message) {
    fprintf(stderr, "Error: %s\n", message);
    return 1;
}

/**
 * Prints one CLI error followed by help.
 * @param message Error text.
 * @return int Always returns 1.
 */
static int inffusion_fail_usage(const char *message) {
    fprintf(stderr, "Error: %s\n\n", message);
    inffusion_help();
    return 1;
}

/**
 * Initializes the runtime configuration with defaults.
 * @param config Output configuration.
 * @return void
 */
static void inffusion_config_init(inffusion_config *config) {
    unsigned int threads = std::thread::hardware_concurrency();

    *config = {};
    config->type = INFUSSION_TYPE_TEXT;
    config->width = INFUSSION_DEFAULT_WIDTH;
    config->height = INFUSSION_DEFAULT_HEIGHT;
    config->steps = INFUSSION_DEFAULT_STEPS;
    config->threads = threads > 0 ? (int)threads : 1;
    config->clip_skip = INFUSSION_DEFAULT_CLIP_SKIP;
    config->cfg_scale = INFUSSION_DEFAULT_CFG;
    config->strength = INFUSSION_DEFAULT_STRENGTH;
    config->seed = INFUSSION_DEFAULT_SEED;
    config->sample_method = INFUSSION_DEFAULT_METHOD;
    config->guidance = INFUSSION_DEFAULT_GUIDANCE;
}

/**
 * Parses one strict integer.
 * @param text Raw input text.
 * @param out Parsed integer output.
 * @return int 0 on success, 1 on failure.
 */
static int inffusion_parse_int(const char *text, int *out) {
    char *end = NULL;
    long value = 0;

    if (!text || !text[0] || !out) {
        return 1;
    }
    errno = 0;
    value = strtol(text, &end, 10);
    if (errno != 0 || !end || *end != '\0' || value < INT_MIN || value > INT_MAX) {
        return 1;
    }
    *out = (int)value;
    return 0;
}

/**
 * Parses one strict signed 64-bit integer.
 * @param text Raw input text.
 * @param out Parsed integer output.
 * @return int 0 on success, 1 on failure.
 */
static int inffusion_parse_i64(const char *text, int64_t *out) {
    char *end = NULL;
    long long value = 0;

    if (!text || !text[0] || !out) {
        return 1;
    }
    errno = 0;
    value = strtoll(text, &end, 10);
    if (errno != 0 || !end || *end != '\0') {
        return 1;
    }
    *out = (int64_t)value;
    return 0;
}

/**
 * Parses one strict float.
 * @param text Raw input text.
 * @param out Parsed float output.
 * @return int 0 on success, 1 on failure.
 */
static int inffusion_parse_float(const char *text, float *out) {
    char *end = NULL;
    float value = 0.0f;

    if (!text || !text[0] || !out) {
        return 1;
    }
    errno = 0;
    value = strtof(text, &end);
    if (errno != 0 || !end || *end != '\0') {
        return 1;
    }
    *out = value;
    return 0;
}

/**
 * Parses one operation type.
 * @param text Raw type text.
 * @param out Parsed type output.
 * @return int 0 on success, 1 on failure.
 */
static int inffusion_parse_type(const char *text, inffusion_type *out) {
    if (!text || !out) {
        return 1;
    }
    if (strcmp(text, "text") == 0) {
        *out = INFUSSION_TYPE_TEXT;
        return 0;
    }
    if (strcmp(text, "image") == 0) {
        *out = INFUSSION_TYPE_IMAGE;
        return 0;
    }
    return 1;
}

/**
 * Validates one output path string.
 * @param path Output path text.
 * @return int 0 on success, 1 on failure.
 */
static int inffusion_validate_output_path(const char *path) {
    if (!path || !path[0]) {
        return 1;
    }
    return strchr(path, '\n') == NULL ? 0 : 1;
}

/**
 * Validates the parsed configuration.
 * @param config Parsed configuration.
 * @return int 0 on success, 1 on failure.
 */
static int inffusion_validate(const inffusion_config *config) {
    if (!config->model_path || !config->model_path[0]) {
        return inffusion_fail_usage("Missing model. Use --model or set INFFUSION_MODEL.");
    }
    if (config->command != INFUSSION_COMMAND_INFER) {
        return inffusion_fail_usage("Missing command. Use 'infer'.");
    }
    if (config->type == INFUSSION_TYPE_IMAGE && config->ref_paths.empty()) {
        return inffusion_fail_usage("infer --type image requires at least one --ref.");
    }
    if (config->output_path && inffusion_validate_output_path(config->output_path) != 0) {
        return inffusion_fail_usage("Invalid --output path.");
    }
    if (config->width <= 0 || config->height <= 0 || config->steps <= 0) {
        return inffusion_fail_usage("Image sizes and steps must be positive.");
    }
    if (config->threads <= 0 || config->clip_skip <= 0) {
        return inffusion_fail_usage("Thread count and clip skip must be positive.");
    }
    if (config->cfg_scale < 0.0f || config->guidance < 0.0f) {
        return inffusion_fail_usage("Guidance values must be non-negative.");
    }
    if (config->strength < 0.0f || config->strength > 1.0f) {
        return inffusion_fail_usage("--strength must be within [0, 1].");
    }
    return 0;
}

/**
 * Parses the command line into the runtime configuration.
 * @param argc Argument count.
 * @param argv Argument vector.
 * @param config Output configuration.
 * @return int 0 on success, 1 on failure, 2 after help.
 */
static int inffusion_parse_args(int argc, char **argv, inffusion_config *config) {
    int i = 0;

    if (argc < 2) {
        return inffusion_fail_usage("Missing command. Use 'infer'.");
    }
    if (strcmp(argv[1], "--version") == 0 || strcmp(argv[1], "-v") == 0) {
        inffusion_version();
        return 2;
    }
    if (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0) {
        inffusion_help();
        return 2;
    }

    inffusion_config_init(config);
    if (strcmp(argv[1], "infer") == 0) {
        config->command = INFUSSION_COMMAND_INFER;
    } else {
        return inffusion_fail_usage("Unknown command. Use 'infer'.");
    }

    for (i = 2; i < argc; ++i) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            inffusion_help();
            return 2;
        } else if (strcmp(argv[i], "--version") == 0 || strcmp(argv[i], "-v") == 0) {
            inffusion_version();
            return 2;
        } else if (strcmp(argv[i], "--model") == 0) {
            if (++i >= argc) return inffusion_fail_usage("Missing value for --model.");
            config->model_path = argv[i];
        } else if (strcmp(argv[i], "--type") == 0) {
            if (++i >= argc) return inffusion_fail_usage("Missing value for --type.");
            if (inffusion_parse_type(argv[i], &config->type) != 0) return inffusion_fail_usage("Invalid value for --type.");
        } else if (strcmp(argv[i], "--negative") == 0) {
            if (++i >= argc) return inffusion_fail_usage("Missing value for --negative.");
            config->negative_prompt = argv[i];
        } else if (strcmp(argv[i], "--ref") == 0) {
            if (++i >= argc) return inffusion_fail_usage("Missing value for --ref.");
            config->ref_paths.push_back(argv[i]);
        } else if (strcmp(argv[i], "--output") == 0) {
            if (++i >= argc) return inffusion_fail_usage("Missing value for --output.");
            config->output_path = argv[i];
        } else if (strcmp(argv[i], "--width") == 0) {
            if (++i >= argc || inffusion_parse_int(argv[i], &config->width) != 0) return inffusion_fail_usage("Invalid value for --width.");
        } else if (strcmp(argv[i], "--height") == 0) {
            if (++i >= argc || inffusion_parse_int(argv[i], &config->height) != 0) return inffusion_fail_usage("Invalid value for --height.");
        } else if (strcmp(argv[i], "--steps") == 0) {
            if (++i >= argc || inffusion_parse_int(argv[i], &config->steps) != 0) return inffusion_fail_usage("Invalid value for --steps.");
        } else if (strcmp(argv[i], "--threads") == 0) {
            if (++i >= argc || inffusion_parse_int(argv[i], &config->threads) != 0) return inffusion_fail_usage("Invalid value for --threads.");
        } else if (strcmp(argv[i], "--cfg") == 0) {
            if (++i >= argc || inffusion_parse_float(argv[i], &config->cfg_scale) != 0) return inffusion_fail_usage("Invalid value for --cfg.");
        } else if (strcmp(argv[i], "--strength") == 0) {
            if (++i >= argc || inffusion_parse_float(argv[i], &config->strength) != 0) return inffusion_fail_usage("Invalid value for --strength.");
        } else if (strcmp(argv[i], "--seed") == 0) {
            if (++i >= argc || inffusion_parse_i64(argv[i], &config->seed) != 0) return inffusion_fail_usage("Invalid value for --seed.");
        } else if (strcmp(argv[i], "--method") == 0) {
            if (++i >= argc) return inffusion_fail_usage("Missing value for --method.");
            config->sample_method = argv[i];
        } else if (strcmp(argv[i], "--vae") == 0) {
            if (++i >= argc) return inffusion_fail_usage("Missing value for --vae.");
            config->vae_path = argv[i];
        } else if (strcmp(argv[i], "--clip-skip") == 0) {
            if (++i >= argc || inffusion_parse_int(argv[i], &config->clip_skip) != 0) return inffusion_fail_usage("Invalid value for --clip-skip.");
        } else if (strcmp(argv[i], "--guidance") == 0) {
            if (++i >= argc || inffusion_parse_float(argv[i], &config->guidance) != 0) return inffusion_fail_usage("Invalid value for --guidance.");
        } else if (strcmp(argv[i], "--lora") == 0) {
            if (++i >= argc) return inffusion_fail_usage("Missing value for --lora.");
            config->lora_paths.push_back(argv[i]);
            config->lora_scales.push_back(1.0f);
        } else if (strcmp(argv[i], "--lora-scale") == 0) {
            if (++i >= argc) return inffusion_fail_usage("Missing value for --lora-scale.");
            if (config->lora_scales.empty()) return inffusion_fail_usage("--lora-scale requires a previous --lora.");
            if (inffusion_parse_float(argv[i], &config->lora_scales.back()) != 0) return inffusion_fail_usage("Invalid value for --lora-scale.");
        } else if (strcmp(argv[i], "--preview") == 0) {
            config->preview = true;
        } else if (strcmp(argv[i], "--vae-tiling") == 0) {
            config->vae_tiling = true;
        } else if (strcmp(argv[i], "--flash-attn") == 0) {
            config->flash_attn = true;
        } else if (strcmp(argv[i], "--cpu-offload") == 0) {
            config->cpu_offload = true;
        } else if (strcmp(argv[i], "--clip-on-cpu") == 0) {
            config->clip_on_cpu = true;
        } else if (strcmp(argv[i], "--vae-on-cpu") == 0) {
            config->vae_on_cpu = true;
        } else {
            return inffusion_fail_usage("Unknown argument.");
        }
    }

    inffusion_resolve_model_path(config);
    return inffusion_validate(config);
}

/**
 * Reads stdin completely into one heap string.
 * @param out_text Output text owner.
 * @return int 0 on success, 1 on failure.
 */
static int inffusion_read_stdin(char **out_text) {
    size_t capacity = 4096;
    size_t length = 0;
    char *buffer = NULL;
    int ch = 0;

    if (!out_text) {
        return 1;
    }
    buffer = (char *)malloc(capacity);
    if (!buffer) {
        return 1;
    }
    while ((ch = fgetc(stdin)) != EOF) {
        if (length + 1 >= capacity) {
            size_t next_capacity = capacity * 2;
            char *next_buffer = (char *)realloc(buffer, next_capacity);
            if (!next_buffer) {
                free(buffer);
                return 1;
            }
            buffer = next_buffer;
            capacity = next_capacity;
        }
        buffer[length++] = (char)ch;
    }
    if (ferror(stdin)) {
        free(buffer);
        return 1;
    }
    while (length > 0 && (buffer[length - 1] == '\n' || buffer[length - 1] == '\r')) {
        --length;
    }
    buffer[length] = '\0';
    *out_text = buffer;
    return 0;
}

/**
 * Builds one default output filename in the current directory.
 * @param out_path Output heap string.
 * @return int 0 on success, 1 on failure.
 */
static int inffusion_make_output_path(char **out_path) {
    char cwd[PATH_MAX];
    char relative_name[128];
    char full_path[PATH_MAX];
    time_t now = 0;

    if (!out_path) {
        return 1;
    }
    now = time(NULL);
#ifdef _WIN32
    if (_getcwd(cwd, sizeof(cwd)) == NULL) {
        return 1;
    }
    if (snprintf(relative_name, sizeof(relative_name), "inffusion-%lld-%lu.png",
        (long long)now, (unsigned long)_getpid()) < 0) {
        return 1;
    }
#else
    if (getcwd(cwd, sizeof(cwd)) == NULL) {
        return 1;
    }
    if (snprintf(relative_name, sizeof(relative_name), "inffusion-%lld-%lu.png",
        (long long)now, (unsigned long)getpid()) < 0) {
        return 1;
    }
#endif
    if (snprintf(full_path, sizeof(full_path), "%s/%s", cwd, relative_name) < 0) {
        return 1;
    }
    *out_path = strdup(full_path);
    return *out_path ? 0 : 1;
}

/**
 * Resolves one output path to an absolute printable path.
 * @param path Input path.
 * @param fallback Output fallback owner.
 * @return const char * Absolute path when available, or the original path.
 */
static const char *inffusion_resolve_output_path(const char *path, char *fallback) {
#ifdef _WIN32
    return _fullpath(fallback, path, PATH_MAX) ? fallback : path;
#else
    return realpath(path, fallback) ? fallback : path;
#endif
}

/**
 * Maps one sampler method name string to the engine enum.
 * @param method Sampler name string.
 * @return enum sample_method_t Mapped sampler enum.
 */
static enum sample_method_t inffusion_map_method(const char *method) {
    if (!method) return EULER_A_SAMPLE_METHOD;
    return str_to_sample_method(method);
}

/**
 * Emits one compact stderr progress bar.
 * @param step Current step.
 * @param steps Total steps.
 * @param elapsed Elapsed seconds.
 * @param data Opaque callback state.
 * @return void
 */
static void inffusion_progress_cb(int step, int steps, float elapsed, void *data) {
    const int width = 24;
    int filled = 0;
    int i = 0;
    float ratio = 0.0f;

    (void)data;
    if (steps <= 0 || step == g_last_progress_step) {
        return;
    }
    g_last_progress_step = step;
    ratio = (float)step / (float)steps;
    if (ratio < 0.0f) ratio = 0.0f;
    if (ratio > 1.0f) ratio = 1.0f;
    filled = (int)(ratio * (float)width);
    fprintf(stderr, "\r[");
    for (i = 0; i < width; ++i) {
        fputc(i < filled ? '#' : '-', stderr);
    }
    fprintf(stderr, "] %3d%% (%d/%d, %.1fs)", (int)(ratio * 100.0f), step, steps, elapsed);
    fflush(stderr);
}

/**
 * Emits engine logs to stderr.
 * @param level Engine log level.
 * @param text Engine log message.
 * @param data Opaque callback state.
 * @return void
 */
static void inffusion_log_cb(enum sd_log_level_t level, const char *text, void *data) {
    (void)data;
    if (!text || !text[0]) {
        return;
    }
    if (level == SD_LOG_DEBUG || level == SD_LOG_INFO) {
        return;
    }
    if (g_last_progress_step >= 0) {
        fputc('\n', stderr);
        g_last_progress_step = -1;
    }
    fputs(text, stderr);
    fflush(stderr);
}

/**
 * Writes one preview frame into the target output file.
 * @param step Current step.
 * @param frame_count Preview frame count.
 * @param frames Preview frames.
 * @param is_noisy Preview state.
 * @param data Opaque callback state.
 * @return void
 */
static void inffusion_preview_cb(int step, int frame_count, sd_image_t *frames, bool is_noisy, void *data) {
    (void)step;
    (void)is_noisy;
    (void)data;
    if (!g_preview_output_path || frame_count <= 0 || !frames || !frames[0].data) {
        return;
    }
    if (!stbi_write_png(
        g_preview_output_path,
        (int)frames[0].width,
        (int)frames[0].height,
        (int)frames[0].channel,
        frames[0].data,
        (int)frames[0].width * (int)frames[0].channel
    )) {
        g_preview_write_failed = true;
    }
}

/**
 * Loads one img2img base image from disk.
 * @param path Input image path.
 * @param image Output image descriptor.
 * @return int 0 on success, 1 on failure.
 */
static int inffusion_load_image(const char *path, sd_image_t *image) {
    int width = 0;
    int height = 0;
    int channel = 0;
    unsigned char *data = NULL;

    if (!path || !image) {
        return 1;
    }
    data = stbi_load(path, &width, &height, &channel, 3);
    if (!data) {
        return 1;
    }
    image->width = (uint32_t)width;
    image->height = (uint32_t)height;
    image->channel = 3;
    image->data = data;
    return 0;
}

/**
 * Builds a full-white img2img mask from one base image.
 * @param init_image Input image.
 * @param mask_image Output mask descriptor.
 * @param mask_data Output heap data owner.
 * @return int 0 on success, 1 on failure.
 */
static int inffusion_make_mask(const sd_image_t *init_image, sd_image_t *mask_image, uint8_t **mask_data) {
    size_t size = 0;

    if (!init_image || !init_image->data || !mask_image || !mask_data) {
        return 1;
    }
    size = (size_t)init_image->width * (size_t)init_image->height;
    *mask_data = (uint8_t *)malloc(size);
    if (!*mask_data) {
        return 1;
    }
    memset(*mask_data, 255, size);
    mask_image->width = init_image->width;
    mask_image->height = init_image->height;
    mask_image->channel = 1;
    mask_image->data = *mask_data;
    return 0;
}

/**
 * Runs the infer operation.
 * @param config Parsed configuration.
 * @return int 0 on success, 1 on failure.
 */
static int inffusion_run_infer(const inffusion_config *config) {
    char *prompt = NULL;
    char *generated_output_path = NULL;
    const char *output_path = config->output_path;
    char absolute_output_path[PATH_MAX];
    sd_ctx_params_t ctx_params;
    sd_img_gen_params_t img_params;
    sd_ctx_t *ctx = NULL;
    sd_image_t init_image;
    sd_image_t mask_image;
    sd_image_t *images = NULL;
    uint8_t *mask_data = NULL;
    std::vector<sd_lora_t> loras;
    int write_ok = 0;

    memset(&init_image, 0, sizeof(init_image));
    memset(&mask_image, 0, sizeof(mask_image));
    memset(absolute_output_path, 0, sizeof(absolute_output_path));
    if (inffusion_read_stdin(&prompt) != 0) {
        return inffusion_fail("Unable to read stdin.");
    }
    if (!prompt[0]) {
        free(prompt);
        return inffusion_fail("Prompt input is empty.");
    }
    if (!output_path) {
        if (inffusion_make_output_path(&generated_output_path) != 0) {
            free(prompt);
            return inffusion_fail("Unable to allocate the default output path.");
        }
        output_path = generated_output_path;
    }
    if (config->type == INFUSSION_TYPE_IMAGE && inffusion_load_image(config->ref_paths.front(), &init_image) != 0) {
        free(prompt);
        free(generated_output_path);
        return inffusion_fail("Unable to load the base image.");
    }

    sd_ctx_params_init(&ctx_params);
    ctx_params.model_path = config->model_path;
    ctx_params.vae_path = config->vae_path;
    ctx_params.n_threads = config->threads;
    ctx_params.rng_type = STD_DEFAULT_RNG;
    ctx_params.sampler_rng_type = STD_DEFAULT_RNG;
    ctx_params.offload_params_to_cpu = config->cpu_offload;
    ctx_params.keep_clip_on_cpu = config->clip_on_cpu;
    ctx_params.keep_vae_on_cpu = config->vae_on_cpu;
    ctx_params.flash_attn = config->flash_attn;
    ctx_params.diffusion_flash_attn = config->flash_attn;

    inffusion_load_backends();
    ctx = new_sd_ctx(&ctx_params);
    if (!ctx) {
        free(prompt);
        free(generated_output_path);
        if (init_image.data) free(init_image.data);
        return inffusion_fail("Unable to initialize stable-diffusion.cpp.");
    }

    sd_img_gen_params_init(&img_params);
    img_params.prompt = prompt;
    img_params.negative_prompt = config->negative_prompt ? config->negative_prompt : "";
    img_params.width = config->width;
    img_params.height = config->height;
    img_params.clip_skip = config->clip_skip;
    img_params.seed = config->seed;
    img_params.batch_count = 1;
    img_params.strength = config->strength;
    img_params.vae_tiling_params.enabled = config->vae_tiling;
    sd_sample_params_init(&img_params.sample_params);
    img_params.sample_params.sample_steps = config->steps;
    img_params.sample_params.sample_method = inffusion_map_method(config->sample_method);
    img_params.sample_params.guidance.txt_cfg = config->cfg_scale;
    img_params.sample_params.guidance.distilled_guidance = config->guidance;

    if (config->type == INFUSSION_TYPE_IMAGE) {
        if (inffusion_make_mask(&init_image, &mask_image, &mask_data) != 0) {
            free_sd_ctx(ctx);
            free(prompt);
            free(generated_output_path);
            free(init_image.data);
            return inffusion_fail("Unable to allocate the img2img mask.");
        }
        img_params.init_image = init_image;
        img_params.mask_image = mask_image;
        img_params.width = (int)init_image.width;
        img_params.height = (int)init_image.height;
    }

    if (!config->lora_paths.empty()) {
        loras.resize(config->lora_paths.size());
        for (size_t i = 0; i < config->lora_paths.size(); ++i) {
            loras[i].path = config->lora_paths[i];
            loras[i].multiplier = config->lora_scales[i];
            loras[i].is_high_noise = false;
        }
        img_params.loras = loras.data();
        img_params.lora_count = (uint32_t)loras.size();
    }

    sd_set_log_callback(inffusion_log_cb, NULL);
    sd_set_progress_callback(inffusion_progress_cb, NULL);
    g_last_progress_step = -1;
    g_preview_write_failed = false;
    g_preview_output_path = NULL;
    if (config->preview) {
        g_preview_output_path = output_path;
        sd_set_preview_callback(inffusion_preview_cb, PREVIEW_PROJ, 1, false, true, NULL);
    }

    images = generate_image(ctx, &img_params);
    sd_set_preview_callback(NULL, PREVIEW_NONE, 0, false, false, NULL);
    g_preview_output_path = NULL;
    if (g_last_progress_step >= 0) {
        fputc('\n', stderr);
    }
    g_last_progress_step = -1;
    if (!images || !images[0].data) {
        if (images) free(images);
        if (mask_data) free(mask_data);
        if (init_image.data) free(init_image.data);
        free_sd_ctx(ctx);
        free(prompt);
        free(generated_output_path);
        return inffusion_fail("Generation failed.");
    }

    write_ok = stbi_write_png(
        output_path,
        (int)images[0].width,
        (int)images[0].height,
        (int)images[0].channel,
        images[0].data,
        (int)images[0].width * (int)images[0].channel
    );
    free(images[0].data);
    free(images);
    if (mask_data) free(mask_data);
    if (init_image.data) free(init_image.data);
    free_sd_ctx(ctx);
    free(prompt);
    if (!write_ok || g_preview_write_failed) {
        free(generated_output_path);
        return inffusion_fail("Unable to write the output image.");
    }

    fprintf(stdout, "%s\n", inffusion_resolve_output_path(output_path, absolute_output_path));
    fflush(stdout);
    free(generated_output_path);
    return 0;
}

/**
 * Main application entry point.
 * @param argc Argument count.
 * @param argv Argument vector.
 * @return int 0 on success, 1 on failure.
 */
int main(int argc, char **argv) {
    inffusion_config config;
    int parse_result = 0;

    inffusion_prepare_stdio();
    parse_result = inffusion_parse_args(argc, argv, &config);
    if (parse_result == 2) {
        return 0;
    }
    if (parse_result != 0) {
        return 1;
    }
    if (config.command == INFUSSION_COMMAND_INFER) {
        return inffusion_run_infer(&config);
    }
    return inffusion_fail_usage("Missing command. Use 'infer'.");
}
