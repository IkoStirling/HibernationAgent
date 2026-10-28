"""Real CPU LoRA training on a locally-created tiny random model, no API/data download."""
import argparse
import importlib.util
import json
import os
from pathlib import Path
import secrets
import subprocess
import sys
import time
import urllib.error
import urllib.request


def run(worker, root):
    root.mkdir(parents=True, exist_ok=False)
    spec = importlib.util.spec_from_file_location("style_worker", worker)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    import torch
    from transformers import LlamaConfig, LlamaForCausalLM, PreTrainedTokenizerFast
    from tokenizers import Tokenizer, models, pre_tokenizers, decoders, Regex
    torch.set_num_threads(2)
    base = root / "tiny-base"
    base.mkdir()
    draft, preferred = "我不清楚，不敢瞎说。", "我不清楚。"
    sample = {"context": [{"role": "user", "text": "它兼容吗？"}], "original": draft,
              "preferred": preferred, "facts": "还没有核对兼容性。", "reason": "保留未知，删除重复保证。",
              "approved": True, "source": "synthetic-fixture"}
    alphabet = set(module.INSTRUCTION + json.dumps(sample, ensure_ascii=False) +
                   "编辑后的对白：draftcontextfacts系统资料" + ''.join(chr(v) for v in range(32, 127)))
    vocab = {"[UNK]": 0, "[EOS]": 1, "[PAD]": 2}
    for char in sorted(alphabet):
        vocab[char] = len(vocab)
    raw = Tokenizer(models.WordLevel(vocab, unk_token="[UNK]"))
    raw.pre_tokenizer = pre_tokenizers.Split(Regex(r"[\s\S]"), behavior="isolated")
    raw.decoder = decoders.Fuse()
    tokenizer = PreTrainedTokenizerFast(tokenizer_object=raw, unk_token="[UNK]", eos_token="[EOS]", pad_token="[PAD]")
    tokenizer.save_pretrained(base)
    torch.manual_seed(17)
    model = LlamaForCausalLM(LlamaConfig(vocab_size=len(vocab), hidden_size=32, intermediate_size=64,
                             num_hidden_layers=2, num_attention_heads=4, num_key_value_heads=2,
                             max_position_embeddings=2048, eos_token_id=1, pad_token_id=2))
    model.save_pretrained(base, safe_serialization=True)
    del model
    token = secrets.token_hex(32)
    data = root / "private"
    ready = root / "ready.json"
    environment = dict(os.environ, HIBERNATION_STYLE_AUTH=token, HF_HUB_OFFLINE="1",
                       TRANSFORMERS_OFFLINE="1", PYTHONDONTWRITEBYTECODE="1")
    child = subprocess.Popen([sys.executable, "-B", str(worker), "--root", str(data),
                              "--ready", str(ready), "--instance", "worker-test"],
                             env=environment, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                             creationflags=subprocess.CREATE_NO_WINDOW)
    settings = {"baseModelPath": str(base), "device": "cpu", "rank": 8, "maxSteps": 120,
                "learningRate": 0.02, "maxSequenceLength": 1024, "generationLimit": 64,
                "trainingTimeoutSeconds": 180, "adapterScale": 1.0}
    checks = []

    def check(value, name):
        assert value, name
        checks.append(name)

    try:
        until = time.monotonic() + 15
        while not ready.exists() and time.monotonic() < until:
            assert child.poll() is None, "worker exited"
            time.sleep(.05)
        port = json.loads(ready.read_text(encoding="utf-8"))["port"]

        def call(path, args=None, auth=token):
            request = urllib.request.Request("http://127.0.0.1:" + str(port) + path,
                      data=json.dumps({"settings": settings, "args": args or {}}, ensure_ascii=False).encode(),
                      headers={"Authorization": "Bearer " + auth, "Content-Type": "application/json"})
            return json.load(urllib.request.urlopen(request, timeout=40))

        try:
            call("/status", auth="wrong")
            raise AssertionError("missing auth accepted")
        except urllib.error.HTTPError as error:
            check(error.code == 403, "private IPC rejects wrong authentication")
        check(call("/status")["result"]["samples"] == 0, "fresh isolated corpus")
        check(call("/train")["error"]["code"] == "approved_corpus_required", "no training without approved material")
        check(call("/import", {"samples": [sample]})["error"]["code"] == "operator_approval_required", "explicit import consent required")
        bad = dict(sample, approved=False)
        check(not call("/import", {"consent": True, "samples": [bad]})["ok"], "each sample also needs approval")
        check(call("/import", {"consent": True, "samples": [sample, sample]})["result"]["samples"] == 1, "approved corpus deduplicates")
        check(preferred.encode() not in (data / "corpus.dpapi").read_bytes(), "corpus is encrypted at rest")
        started = call("/train")
        check(started["ok"] and not started["result"]["automatically_activated"], "real training is queued without activation")
        until = time.monotonic() + 180
        while time.monotonic() < until:
            status = call("/status")["result"]
            if status["job"]["state"] in ("completed", "failed", "cancelled"):
                break
            time.sleep(.1)
        check(status["job"]["state"] == "completed", "actual PEFT training completed: " + str(status["job"].get("error", "")))
        info = status["adapters"][-1]
        check(info["final_loss"] < info["first_loss"], "actual adapter gradients reduce supervised loss")
        settings["adapterId"] = info["id"]
        blob = data / (info["id"] + ".dpapi")
        check(not blob.read_bytes().startswith(b"{") and preferred.encode() not in blob.read_bytes(), "adapter weights and manifest are encrypted")
        check(not list(data.glob("*.safetensors")) and not list(data.glob("*.jsonl")), "no plaintext training or adapter temporary files")
        output = call("/preview", {"text": draft, "context": sample["context"], "facts": sample["facts"]})
        check(output["ok"] and output["result"]["text"] == preferred, "encrypted adapter reload produces the trained tiny-fixture edit")
        settings["adapterScale"] = 0
        check(call("/preview", {"text": draft})["result"]["text"] == draft, "zero strength bypasses editing")
        settings["adapterScale"] = 1
        settings["adapterId"] = "../../not-an-adapter"
        check(not call("/preview", {"text": draft})["ok"], "adapter traversal is rejected")
        settings.pop("adapterId")
        settings["maxSteps"] = 5000
        next_job = call("/train")
        check(next_job["ok"], "second bounded batch starts")
        call("/cancel")
        until = time.monotonic() + 30
        while time.monotonic() < until:
            status = call("/status")["result"]
            if status["job"]["state"] in ("cancelled", "failed", "completed"):
                break
            time.sleep(.1)
        check(status["job"]["state"] == "cancelled" and len(status["adapters"]) == 1, "cancelled training does not publish a usable adapter")
        evidence = {"completed": True, "checks": checks, "real_training": True,
                    "model": "locally-created tiny random Llama; not a language-quality claim",
                    "api_calls": 0, "downloads": 0, "private_production_data_used": False,
                    "first_loss": info["first_loss"], "final_loss": info["final_loss"],
                    "base_path": str(base), "torch": torch.__version__}
        (root / "acceptance.json").write_text(json.dumps(evidence, ensure_ascii=False, indent=2), encoding="utf-8")
        print(json.dumps(evidence, ensure_ascii=False))
    finally:
        child.terminate()
        child.wait(timeout=15)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--worker", type=Path, required=True)
    parser.add_argument("--root", type=Path, required=True)
    args = parser.parse_args()
    run(args.worker.resolve(), args.root.resolve().with_name(args.root.name + "-" + secrets.token_hex(4)))
