#!/usr/bin/env python3
from pathlib import Path
import sys

TEMPLATE = r'''#include <Arduino.h>
#include "demo_log.h"
#include "demo_check.h"
#include "demo_config.h"
#include "demo_pins.h"

static const char *TAG = "{tag}";

static bool run_demo()
{{
    DEMO_STEP("TODO: 补充演示步骤");
    DEMO_CHECK_TRUE(true, "placeholder");
    return true;
}}

void setup()
{{
    Serial.begin(DEMO_BAUDRATE);
    delay(DemoConfig::SERIAL_WAIT_MS);

    DEMO_BEGIN(TAG);
    if (run_demo()) {{
        DEMO_PASS(TAG);
    }} else {{
        DEMO_FAIL(TAG);
    }}
    DEMO_END(TAG);
}}

void loop() {{}}
'''


def parse_args(argv: list[str]) -> tuple[str, str, str]:
    style = "flat"
    positional: list[str] = []
    i = 0

    while i < len(argv):
        arg = argv[i]
        if arg == "--style":
            i += 1
            if i >= len(argv):
                raise ValueError("missing value for --style")
            style = argv[i].strip().lower()
        else:
            positional.append(arg)
        i += 1

    if len(positional) != 2:
        raise ValueError("Usage: scaffold_example.py <module> <case_name> [--style flat|nested]")

    if style not in {"flat", "nested"}:
        raise ValueError("style must be flat or nested")

    module = positional[0].strip()
    case_name = positional[1].strip()
    if not module or not case_name:
        raise ValueError("module and case_name must be non-empty")

    return module, case_name, style


def main() -> int:
    try:
        module, case_name, style = parse_args(sys.argv[1:])
    except ValueError as exc:
        print(exc)
        return 1

    root = Path.cwd()
    if style == "nested":
        target_dir = root / "examples" / module / case_name
    else:
        target_dir = root / "examples" / f"{module}_{case_name}"

    target_dir.mkdir(parents=True, exist_ok=True)
    out = target_dir / "main.cpp"
    if out.exists():
        print(f"Exists: {out}")
        return 0

    out.write_text(TEMPLATE.format(tag=f"{module}_{case_name}"), encoding="utf-8")
    print(f"Created: {out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
