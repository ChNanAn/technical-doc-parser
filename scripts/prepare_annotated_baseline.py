#!/usr/bin/env python3

import hashlib
import json
import tarfile
import urllib.request
import xml.etree.ElementTree as ET
from collections import defaultdict
from pathlib import Path

try:
    from PIL import Image
except ImportError as error:
    raise SystemExit("Install dataset dependencies with: python3 -m pip install pillow") from error


ROOT = Path(__file__).resolve().parents[1]
CACHE_ROOT = ROOT / "data" / "raw" / "quality_baseline" / "annotated_cache"
OUTPUT_ROOT = ROOT / "tests" / "benchmark" / "corpus"

TESSERACT_COMMIT = "232ff181c66516116ec0e84c4963f70de15050fd"
TESSERACT_RAW_URL = f"https://raw.githubusercontent.com/tesseract-ocr/test/{TESSERACT_COMMIT}/testing"
TESSERACT_LICENSE_URL = f"https://raw.githubusercontent.com/tesseract-ocr/test/{TESSERACT_COMMIT}/LICENSE"
TESSERACT_LICENSE_SHA256 = "c71d239df91726fc519c6eb72d318ec65820627232b2f796219e87dcf35d0ab4"
TESSERACT_SAMPLES = [
    {
        "id": "eurotext",
        "image": "eurotext.tif",
        "image_sha256": "7b9bd14aba7d5e30df686fbb6f71782a97f48f81b32dc201a1b75afe6de747d6",
        "text_source": "eurotext.txt",
        "text_sha256": "26a43b5107b51054910b89f6fea67a03125b5737c0200e588121d4d132c7f3cb",
    },
    {
        "id": "phototest",
        "image": "phototest.tif",
        "image_sha256": "d2241a1eb6d6cd2eb6544b8e228c2862c852b7467d16ed636caa32179b780692",
        "text_source": "phototest.gold.txt",
        "text_sha256": "76ef157baeb6ab275b7d90f310477a8060d5822aea2b2e05d480f459b016a6ed",
    },
    {
        "id": "phototest_rotated_180",
        "image": "phototest-rotated-180.png",
        "image_sha256": "9a2ceaab3685a36a2041ef49b5683db2bedb4ff82d87136be4b52afa030e8114",
        "text_source": "phototest.gold.txt",
        "text_sha256": "76ef157baeb6ab275b7d90f310477a8060d5822aea2b2e05d480f459b016a6ed",
    },
    {
        "id": "unlv_8071_093_3b",
        "image": "8071_093.3B.tif",
        "image_sha256": "d4f01cba19c99f8894d94a6d43eb8ed8013f8cf17fc08af9346bb9fb3697d452",
        "text_source": "8071_093.3B.txt",
        "text_sha256": "29f290e47012a809f5139c796493b3c693e44e009dd8fb3dafe15420e8b4c7d5",
    },
    {
        "id": "unlv_8087_054_3b",
        "image": "8087_054.3B.tif",
        "image_sha256": "dab6db0f4c32296f313c7f1e7e139b13d7c69be65c64d6016f85ea67ebca9102",
        "text_source": "8087_054.3B.txt",
        "text_sha256": "9965c66f974f85c25e51041b47aceed287dd2302abd64d7e969af674dca83edb",
    },
]

DOCLAYNET_URL = (
    "https://codait-cos-dax.s3.us.cloud-object-storage.appdomain.cloud/"
    "dax-doclaynet/1.0.0/DocLayNet_core.zip"
)
DOCLAYNET_REPOSITORY_COMMIT = "66947398f04d050fed84e89e5509828f2ee17ecf"
DOCLAYNET_LICENSE_URL = (
    f"https://raw.githubusercontent.com/DS4SD/DocLayNet/{DOCLAYNET_REPOSITORY_COMMIT}/LICENSE"
)
DOCLAYNET_LICENSE_SHA256 = "2d6f51b40d9b47e2b01e1503e8e16230b5745bc927ae9024f734e36fcee8e7f5"
DOCLAYNET_TEST_JSON_SHA256 = "37ccce8c7951dcfd9c7c832bbff5aee71cb59c032ed7021e125e6188e6ac4fe1"
DOCLAYNET_IMAGE_IDS = [1736, 1847, 3647, 4264, 4714]
DOCLAYNET_IMAGE_SHA256 = {
    1736: "9cef129c342d3cab478b09afc87d5993e5b291012ee3bf54705053574b6233d3",
    1847: "d24b8b4572a9b10d46dace561039a31a4439c1a6a05c95e86eeccea62aea6a08",
    3647: "11afc14875b435b924ffd75bddf5f5b62d688b44491ab59ad2966aefde1783fa",
    4264: "1388fd0a13ee85a314e1723ddde46c91ab3186103854f965f0d47ddcef773ca7",
    4714: "66972b61325ad95fb563ef1379779bbf3d6b8fb7cf6bc6e7f26a481ced1bdfd0",
}
DOCLAYNET_LABEL_MAP = {
    "Caption": "text",
    "Footnote": "footer",
    "Formula": "unknown",
    "List-item": "list",
    "Page-footer": "footer",
    "Page-header": "header",
    "Picture": "figure",
    "Section-header": "title",
    "Table": "table",
    "Text": "text",
    "Title": "title",
}

PUBTABLES_COMMIT = "35b1c097807e0b07ec5313879b85956b7b3890db"
PUBTABLES_BASE_URL = f"https://huggingface.co/datasets/bsmock/pubtables-1m/resolve/{PUBTABLES_COMMIT}"
PUBTABLES_ANNOTATION_URL = f"{PUBTABLES_BASE_URL}/PubTables-1M-Structure_Annotations_Test.tar.gz"
PUBTABLES_IMAGE_URL = f"{PUBTABLES_BASE_URL}/PubTables-1M-Structure_Images_Test.tar.gz"
PUBTABLES_ANNOTATION_SHA256 = "be13440e09f9ecc2b502dc87898420080bf572986a2ae74e0b8713d5cebaa384"
PUBTABLES_IDS = [
    "PMC4840909_table_0",
    "PMC3430698_table_1",
    "PMC4429321_table_0",
    "PMC3430698_table_0",
    "PMC1187882_table_1",
]
PUBTABLES_IMAGE_SHA256 = {
    "PMC4840909_table_0": "c2f3d8dccfc41cb63d2dcb56bd11c17b24c65afff40cfb1e5b02721e4fce5d91",
    "PMC3430698_table_1": "d1ead936575172221667ef920d222b78cbc84fc3d9dd1b97dd9e6a2f69382316",
    "PMC4429321_table_0": "295c69bdfa20a95ea215531546c48c5a9def60869c952fac147420b22becfead",
    "PMC3430698_table_0": "b291ddfc48e641dc0ff007d8b3a720a8ad51a27dee186f8d0cb1e0d2f5047f18",
    "PMC1187882_table_1": "32328b9feae15e66527c9bf92509e8d832c9f258a2ea54bf7f88076676f198b9",
}


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def download(url: str, output_path: Path, expected_sha256: str) -> None:
    if output_path.is_file() and sha256(output_path) == expected_sha256:
        print(f"Using verified file: {output_path.name}")
        return
    output_path.parent.mkdir(parents=True, exist_ok=True)
    temporary_path = output_path.with_suffix(output_path.suffix + ".tmp")
    print(f"Downloading {url}")
    urllib.request.urlretrieve(url, temporary_path)
    actual_sha256 = sha256(temporary_path)
    if actual_sha256 != expected_sha256:
        temporary_path.unlink(missing_ok=True)
        raise RuntimeError(f"SHA256 mismatch for {output_path.name}: {actual_sha256}")
    temporary_path.replace(output_path)


def image_size(path: Path) -> tuple[int, int]:
    with Image.open(path) as image:
        return image.size


def remote_zip(url: str):
    try:
        from remotezip import RemoteZip
    except ImportError as error:
        raise SystemExit("Install the DocLayNet download dependency with: python3 -m pip install remotezip") from error
    return RemoteZip(url)


def validate_boxes(sample_id: str, width: int, height: int, objects: list[dict]) -> None:
    for item in objects:
        x0, y0, x1, y1 = item["bbox"]
        if not (0 <= x0 <= x1 <= width + 1 and 0 <= y0 <= y1 <= height + 1):
            raise RuntimeError(f"bbox outside image for {sample_id}: {item['bbox']}")


def prepare_tesseract() -> dict:
    output_root = OUTPUT_ROOT / "ocr_tesseract"
    image_dir = output_root / "images"
    transcript_dir = output_root / "transcripts"
    image_dir.mkdir(parents=True, exist_ok=True)
    transcript_dir.mkdir(parents=True, exist_ok=True)

    samples = []
    for source in TESSERACT_SAMPLES:
        sample_id = source["id"]
        output_image = image_dir / source["image"]
        output_transcript = transcript_dir / f"{sample_id}.txt"
        download(f"{TESSERACT_RAW_URL}/{source['image']}", output_image, source["image_sha256"])
        download(
            f"{TESSERACT_RAW_URL}/{source['text_source']}",
            output_transcript,
            source["text_sha256"],
        )
        width, height = image_size(output_image)
        text = output_transcript.read_text(encoding="utf-8")
        samples.append(
            {
                "id": sample_id,
                "image": f"images/{output_image.name}",
                "transcript": f"transcripts/{output_transcript.name}",
                "width": width,
                "height": height,
                "text": text,
                "image_sha256": sha256(output_image),
                "transcript_sha256": sha256(output_transcript),
            }
        )

    ground_truth_path = output_root / "ground_truth.json"
    ground_truth_path.write_text(
        json.dumps(
            {"version": 1, "task": "ocr_text", "dataset": "tesseract-ocr/test", "samples": samples},
            indent=2,
            ensure_ascii=False,
        )
        + "\n",
        encoding="utf-8",
    )
    return {
        "dataset": "tesseract-ocr/test",
        "task": "ocr_text",
        "samples": len(samples),
        "ground_truth": ground_truth_path,
    }


def prepare_doclaynet() -> dict:
    output_root = OUTPUT_ROOT / "layout_doclaynet"
    raw_dir = CACHE_ROOT / "doclaynet"
    image_dir = output_root / "images"
    raw_dir.mkdir(parents=True, exist_ok=True)
    image_dir.mkdir(parents=True, exist_ok=True)
    test_json_path = raw_dir / "test.json"

    if not test_json_path.is_file() or sha256(test_json_path) != DOCLAYNET_TEST_JSON_SHA256:
        with remote_zip(DOCLAYNET_URL) as archive:
            print("Downloading DocLayNet test annotations with HTTP Range")
            test_json_path.write_bytes(archive.read("COCO/test.json"))
    if sha256(test_json_path) != DOCLAYNET_TEST_JSON_SHA256:
        raise RuntimeError("DocLayNet test.json SHA256 mismatch")

    dataset = json.loads(test_json_path.read_text(encoding="utf-8"))
    images_by_id = {image["id"]: image for image in dataset["images"]}
    categories = {category["id"]: category["name"] for category in dataset["categories"]}
    annotations_by_image = defaultdict(list)
    for annotation in dataset["annotations"]:
        annotations_by_image[annotation["image_id"]].append(annotation)

    missing_image_ids = []
    for image_id in DOCLAYNET_IMAGE_IDS:
        image_path = image_dir / images_by_id[image_id]["file_name"]
        if not image_path.is_file() or sha256(image_path) != DOCLAYNET_IMAGE_SHA256[image_id]:
            missing_image_ids.append(image_id)
    if missing_image_ids:
        with remote_zip(DOCLAYNET_URL) as archive:
            for image_id in missing_image_ids:
                image_name = images_by_id[image_id]["file_name"]
                output_image = image_dir / image_name
                print(f"Downloading DocLayNet image {image_name} with HTTP Range")
                output_image.write_bytes(archive.read(f"PNG/{image_name}"))
                if sha256(output_image) != DOCLAYNET_IMAGE_SHA256[image_id]:
                    raise RuntimeError(f"DocLayNet image SHA256 mismatch: {image_name}")

    samples = []
    selected_annotations = []
    for image_id in DOCLAYNET_IMAGE_IDS:
        image = images_by_id[image_id]
        image_name = image["file_name"]
        output_image = image_dir / image_name
        objects = []
        for annotation in annotations_by_image[image_id]:
            x, y, width, height = annotation["bbox"]
            label = categories[annotation["category_id"]]
            objects.append(
                {
                    "id": annotation["id"],
                    "label": label,
                    "mapped_label": DOCLAYNET_LABEL_MAP[label],
                    "bbox": [x, y, x + width, y + height],
                }
            )
            selected_annotations.append(annotation)
        actual_width, actual_height = image_size(output_image)
        if (actual_width, actual_height) != (image["width"], image["height"]):
            raise RuntimeError(f"DocLayNet image dimensions differ: {image_name}")
        validate_boxes(str(image_id), image["width"], image["height"], objects)
        samples.append(
            {
                "id": image_id,
                "image": f"images/{image_name}",
                "width": image["width"],
                "height": image["height"],
                "doc_category": image.get("doc_category"),
                "objects": objects,
                "image_sha256": sha256(output_image),
            }
        )

    selected_images = [images_by_id[image_id] for image_id in DOCLAYNET_IMAGE_IDS]
    subset_coco = {
        "info": dataset.get("info", {}),
        "licenses": dataset.get("licenses", []),
        "categories": dataset["categories"],
        "images": selected_images,
        "annotations": selected_annotations,
    }
    subset_path = output_root / "subset.coco.json"
    subset_path.write_text(json.dumps(subset_coco, indent=2) + "\n", encoding="utf-8")
    ground_truth_path = output_root / "ground_truth.json"
    ground_truth_path.write_text(
        json.dumps(
            {
                "version": 1,
                "task": "layout",
                "dataset": "DocLayNet",
                "label_map": DOCLAYNET_LABEL_MAP,
                "samples": samples,
            },
            indent=2,
        )
        + "\n",
        encoding="utf-8",
    )
    return {"dataset": "DocLayNet", "task": "layout", "samples": len(samples), "ground_truth": ground_truth_path}


def parse_pubtables_xml(xml_path: Path) -> dict:
    root = ET.parse(xml_path).getroot()
    width = int(root.findtext("size/width", "0"))
    height = int(root.findtext("size/height", "0"))
    objects = []
    for item in root.findall("object"):
        box = item.find("bndbox")
        objects.append(
            {
                "label": item.findtext("name", "unknown"),
                "bbox": [
                    float(box.findtext("xmin", "0")),
                    float(box.findtext("ymin", "0")),
                    float(box.findtext("xmax", "0")),
                    float(box.findtext("ymax", "0")),
                ],
            }
        )
    return {"width": width, "height": height, "objects": objects}


def prepare_pubtables() -> dict:
    output_root = OUTPUT_ROOT / "table_pubtables"
    raw_dir = CACHE_ROOT / "pubtables"
    image_dir = output_root / "images"
    annotation_dir = output_root / "annotations"
    raw_dir.mkdir(parents=True, exist_ok=True)
    image_dir.mkdir(parents=True, exist_ok=True)
    annotation_dir.mkdir(parents=True, exist_ok=True)

    annotation_archive_path = raw_dir / "structure_annotations_test.tar.gz"
    download(PUBTABLES_ANNOTATION_URL, annotation_archive_path, PUBTABLES_ANNOTATION_SHA256)
    targets = {f"{sample_id}.xml" for sample_id in PUBTABLES_IDS}
    with tarfile.open(annotation_archive_path, "r:gz") as archive:
        members = {Path(member.name).name: member for member in archive.getmembers() if member.isfile()}
        for target in targets:
            source = archive.extractfile(members[target])
            if source is None:
                raise RuntimeError(f"missing PubTables annotation: {target}")
            (annotation_dir / target).write_bytes(source.read())

    image_targets = {f"{sample_id}.jpg" for sample_id in PUBTABLES_IDS}
    missing_images = {
        name
        for name in image_targets
        if not (image_dir / name).is_file()
        or sha256(image_dir / name) != PUBTABLES_IMAGE_SHA256[Path(name).stem]
    }
    if missing_images:
        print("Streaming PubTables test archive until the five fixed images are found")
        with urllib.request.urlopen(PUBTABLES_IMAGE_URL) as response:
            with tarfile.open(fileobj=response, mode="r|gz") as archive:
                for member in archive:
                    name = Path(member.name).name
                    if not member.isfile() or name not in missing_images:
                        continue
                    source = archive.extractfile(member)
                    if source is None:
                        continue
                    (image_dir / name).write_bytes(source.read())
                    missing_images.remove(name)
                    print(f"Extracted PubTables image {name}")
                    if not missing_images:
                        break
        if missing_images:
            raise RuntimeError(f"missing PubTables images: {sorted(missing_images)}")

    samples = []
    for sample_id in PUBTABLES_IDS:
        image_path = image_dir / f"{sample_id}.jpg"
        annotation_path = annotation_dir / f"{sample_id}.xml"
        parsed = parse_pubtables_xml(annotation_path)
        if sha256(image_path) != PUBTABLES_IMAGE_SHA256[sample_id]:
            raise RuntimeError(f"PubTables image SHA256 mismatch: {image_path.name}")
        actual_width, actual_height = image_size(image_path)
        if (actual_width, actual_height) != (parsed["width"], parsed["height"]):
            raise RuntimeError(f"PubTables image dimensions differ: {image_path.name}")
        validate_boxes(sample_id, parsed["width"], parsed["height"], parsed["objects"])
        samples.append(
            {
                "id": sample_id,
                "image": f"images/{image_path.name}",
                "annotation": f"annotations/{annotation_path.name}",
                "width": parsed["width"],
                "height": parsed["height"],
                "objects": parsed["objects"],
                "image_sha256": sha256(image_path),
                "annotation_sha256": sha256(annotation_path),
            }
        )

    ground_truth_path = output_root / "ground_truth.json"
    ground_truth_path.write_text(
        json.dumps({"version": 1, "task": "table_structure", "dataset": "PubTables-1M", "samples": samples}, indent=2)
        + "\n",
        encoding="utf-8",
    )
    return {"dataset": "PubTables-1M", "task": "table_structure", "samples": len(samples), "ground_truth": ground_truth_path}


def main() -> int:
    OUTPUT_ROOT.mkdir(parents=True, exist_ok=True)
    license_dir = OUTPUT_ROOT / "licenses"
    license_dir.mkdir(parents=True, exist_ok=True)
    download(TESSERACT_LICENSE_URL, license_dir / "TESSERACT_TEST_APACHE_2.0.txt", TESSERACT_LICENSE_SHA256)
    download(DOCLAYNET_LICENSE_URL, license_dir / "DOCLAYNET_CDLA_PERMISSIVE_1.0.txt", DOCLAYNET_LICENSE_SHA256)
    results = [prepare_tesseract(), prepare_doclaynet(), prepare_pubtables()]
    manifest = {
        "version": 1,
        "sample_count": sum(result["samples"] for result in results),
        "datasets": [
            {
                "dataset": result["dataset"],
                "task": result["task"],
                "samples": result["samples"],
                "ground_truth": str(result["ground_truth"].relative_to(OUTPUT_ROOT)),
                "ground_truth_sha256": sha256(result["ground_truth"]),
            }
            for result in results
        ],
    }
    manifest_path = OUTPUT_ROOT / "manifest.json"
    manifest_path.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(f"Prepared {manifest['sample_count']} redistributable annotated samples under {OUTPUT_ROOT}")
    return 0 if manifest["sample_count"] == 15 else 1


if __name__ == "__main__":
    raise SystemExit(main())
