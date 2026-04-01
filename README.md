![inffusion](./logo.png)

# inffusion - Raw stable-diffusion.cpp core binary

`inffusion` is a small autonomous and portable base binary built directly on
top of `stable-diffusion.cpp`.

`inffusion` uses a compact self-contained layout with vendored native
dependencies, native installers, and a raw shell-facing interface.

The public surface is intentionally small:
- `inffusion infer`

## Interface

`inffusion` follows a verb-first shell pattern and keeps the I/O contract
simple:
- `stdin` carries the dynamic prompt
- `stderr` carries generation progress and runtime notices
- the generated image is written to a real file path
- `stdout` prints only the final image path

The CLI starts from:

```bash
inffusion infer [options]
```

Operational rules:
- `--type` defaults to `text`
- `--ref` is repeatable and represents reference images
- if `--output` is omitted, `inffusion` creates a unique `.png` in the current directory
- `--preview`, when enabled, updates the same output file during generation
- `--model` wins when passed; otherwise `INFFUSION_MODEL` is used if present

## Infer

Text-to-image from `stdin`:

```bash
printf 'A foggy port at dawn.' | inffusion infer --model /path/to/model.safetensors
```

Reference-guided generation:

```bash
printf 'Combine both references into one image.' | inffusion infer \
    --model /path/to/model.safetensors \
    --ref /path/to/one.png \
    --ref /path/to/two.png
```

Img2img:

```bash
printf 'Turn it into a rainy neon night.' | inffusion infer \
    --type image \
    --model /path/to/model.safetensors \
    --ref /path/to/base.png
```

Preview on the final output path:

```bash
printf 'A brutalist city at sunset.' | inffusion infer \
    --model /path/to/model.safetensors \
    --output /path/to/output.png \
    --preview
```

## Parameters

| Flag | Description | Default |
| :--- | :--- | :--- |
| `--model` | Path to the model file | `INFFUSION_MODEL` or required |
| `--type` | Operation input type | `text` |
| `--ref` | Reference image path, repeatable | `NULL` |
| `--output` | Output image path | `auto` |
| `--negative` | Negative prompt | `NULL` |
| `--width` | Output width | `1024` |
| `--height` | Output height | `1024` |
| `--steps` | Inference steps | `20` |
| `--threads` | CPU worker threads | `auto` |
| `--cfg` | CFG scale | `7.0` |
| `--strength` | Img2img strength | `0.75` |
| `--seed` | RNG seed | `-1` |
| `--method` | Sampling method | `euler_a` |
| `--vae` | External VAE path | `NULL` |
| `--clip-skip` | CLIP skip count | `1` |
| `--lora` | LoRA adapter path, repeatable | `NULL` |
| `--lora-scale` | Scale for the previous LoRA entry | `1.0` |
| `--guidance` | Distilled guidance scale | `3.5` |
| `--preview` | Update the output image during generation | `false` |
| `--vae-tiling` | Enable VAE tiling | `false` |
| `--flash-attn` | Enable Flash Attention | `false` |
| `--cpu-offload` | Offload weights to RAM | `false` |
| `--clip-on-cpu` | Keep CLIP on CPU | `false` |
| `--vae-on-cpu` | Keep VAE on CPU | `false` |

## Dependencies

`inffusion` carries its native build dependencies inside this repository under
`lib/`.

Layout:
- `lib/inc` contains the headers used by the build
- `lib/obj` contains the shared runtime libraries used for the final link on each supported target

## Local Build

```bash
make x86_64
make aarch64
make arm64-v8a
make win64
make all
```

---

**Author:** KaisarCode

**Email:** <kaisar@kaisarcode.com>

**Website:** [https://kaisarcode.com](https://kaisarcode.com)

**License:** [GNU GPL v3.0](https://www.gnu.org/licenses/gpl-3.0.html)

© 2026 KaisarCode
