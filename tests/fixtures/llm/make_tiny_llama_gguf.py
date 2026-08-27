#!/usr/bin/env python3
"""Regenerate the checked-in tiny-llama GGUF fixture used by the real-LLM run
evidence test (KR4.5 / RFC 0012 stabilized).

The fixture is a genuine llama-architecture model with RANDOM (untrained)
weights. llama.cpp loads it and performs real autoregressive transformer
inference over it — every generated token comes from a real forward pass plus
sampler, not a canned lookup. Untrained weights are fine here: the point of the
evidence is that AHFL's production LLM provider path drives a real inference
engine end to end, not that the model is smart.

This script is committed so the binary fixture is reproducible. It requires the
`gguf` and `numpy` Python packages (only needed to REGENERATE the fixture; the
evidence test itself consumes the committed .gguf and needs neither).

Usage:
    python3 make_tiny_llama_gguf.py [output.gguf]
"""
from __future__ import annotations

import sys

import gguf
import numpy as np

# Vocab MUST include the 256 byte-fallback tokens (<0x00>..<0xFF>, type BYTE) so
# the SPM tokenizer can encode ANY input by decomposing unknown chars into
# bytes. Without them, encoding throws unordered_map::at on the first unseen
# character. n_ctx is deliberately large enough for AHFL's ~430-token capability
# prompt (system instructions + JSON schema + input).
ARCH = "llama"
N_EMBD = 16
N_HEAD = 2
N_LAYER = 1
N_FF = 32
N_CTX = 2048
HEAD_DIM = N_EMBD // N_HEAD


def main() -> int:
    out = sys.argv[1] if len(sys.argv) > 1 else "tiny-llama.gguf"

    specials = ["<unk>", "<s>", "</s>"]
    normals = ["▁"] + [chr(ord("a") + i) for i in range(26)]  # SPM space + a-z
    byte_tokens = [f"<0x{b:02X}>" for b in range(256)]
    tokens = specials + normals + byte_tokens
    n_vocab = len(tokens)

    scores = [0.0] * len(specials) + [-1.0] * len(normals) + [0.0] * len(byte_tokens)
    # token types: 3=CONTROL, 1=NORMAL, 6=BYTE
    toktypes = [3, 3, 3] + [1] * len(normals) + [6] * len(byte_tokens)

    w = gguf.GGUFWriter(out, ARCH)
    w.add_context_length(N_CTX)
    w.add_embedding_length(N_EMBD)
    w.add_block_count(N_LAYER)
    w.add_feed_forward_length(N_FF)
    w.add_head_count(N_HEAD)
    w.add_head_count_kv(N_HEAD)
    w.add_rope_dimension_count(HEAD_DIM)
    w.add_layer_norm_rms_eps(1e-5)
    w.add_file_type(0)  # all-F32

    w.add_tokenizer_model("llama")
    w.add_token_list(tokens)
    w.add_token_scores(scores)
    w.add_token_types(toktypes)
    w.add_bos_token_id(1)
    w.add_eos_token_id(2)
    w.add_unk_token_id(0)

    rng = np.random.default_rng(1234)  # fixed seed -> reproducible fixture

    def rnd(*shape: int) -> np.ndarray:
        return rng.standard_normal(shape).astype(np.float32) * 0.02

    w.add_tensor("token_embd.weight", rnd(n_vocab, N_EMBD))
    w.add_tensor("output_norm.weight", np.ones(N_EMBD, dtype=np.float32))
    w.add_tensor("output.weight", rnd(n_vocab, N_EMBD))
    for i in range(N_LAYER):
        p = f"blk.{i}."
        w.add_tensor(p + "attn_norm.weight", np.ones(N_EMBD, dtype=np.float32))
        w.add_tensor(p + "attn_q.weight", rnd(N_EMBD, N_EMBD))
        w.add_tensor(p + "attn_k.weight", rnd(N_EMBD, N_EMBD))
        w.add_tensor(p + "attn_v.weight", rnd(N_EMBD, N_EMBD))
        w.add_tensor(p + "attn_output.weight", rnd(N_EMBD, N_EMBD))
        w.add_tensor(p + "ffn_norm.weight", np.ones(N_EMBD, dtype=np.float32))
        w.add_tensor(p + "ffn_gate.weight", rnd(N_FF, N_EMBD))
        w.add_tensor(p + "ffn_up.weight", rnd(N_FF, N_EMBD))
        w.add_tensor(p + "ffn_down.weight", rnd(N_EMBD, N_FF))

    w.write_header_to_file()
    w.write_kv_data_to_file()
    w.write_tensors_to_file()
    w.close()
    print(f"wrote {out} n_vocab={n_vocab}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
