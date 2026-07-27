# Model Pack Licenses

The model pack is a convenience distribution of third-party model weights and
configuration files. It is separate from the MIT-licensed engine source and
program packages.

| Family | Files | SPDX license | Source |
| --- | --- | --- | --- |
| PaddleOCR v5 mobile | OCR detection, recognition, dictionary | Apache-2.0 | PaddlePaddle/PaddleOCR and pinned Hugging Face repositories |
| RF-DETR DocLayNet | Layout model | MIT | neka-nat/rfdetr-doclaynet-onnx |
| PP-DocLayoutV3 | Layout model and inference configuration | Apache-2.0 | PaddlePaddle/PP-DocLayoutV3_onnx |
| Table Transformer | Detection and structure models | MIT | Xenova conversions of microsoft/table-transformer |

`MODEL-MANIFEST.json` is the authoritative per-file record. It contains the
exact upstream repository, immutable revision and URL, SHA256, SPDX identifier,
and license source URL for every included file.

Users are responsible for reviewing the upstream licenses and model or dataset
terms for their deployment context. Inclusion in this pack does not change the
upstream license and does not grant rights beyond those terms.
