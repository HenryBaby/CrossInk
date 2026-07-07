#!/usr/bin/env python3
"""
Generate GitHub release notes from CHANGELOG.md.

The release workflow uses this so tags, firmware binaries, and release notes
share one source of truth.
"""

import argparse
import re
from datetime import datetime, timedelta, timezone
from pathlib import Path


SECTION_TITLE_MAP = {
    'Added': 'New',
    'Changed': 'Changed',
    'Deprecated': 'Deprecated',
    'Removed': 'Removed',
    'Fixed': 'Fixed',
    'Security': 'Security',
}

TIP = """\
---

Tip

If you experience any problems, please clear your caches before opening an issue. Start with the least invasive and work your way to the most invasive if problems persist after each step.
1. Delete book cache (In-reader menu > `Delete book cache`)
2. From your SD card: Delete the individual `.crosspoint/epub_<hash>` folder for the book giving you issues
3. Delete all reading cache (`Settings > System > Files & Cache > Clear Reading Cache`)
4. From your SD card: Delete ALL `.crosspoint/epub_<hash>` folders and `recent.json` and `state.json`
5. Back up your `global_stats.bin` and then delete the entire `.crosspoint/` folder
"""


def normalize_version(version):
    version = version.strip()
    return version[1:] if version.startswith('v') else version


def extract_version_section(changelog, version):
    wanted = normalize_version(version)
    heading = re.compile(r'^## \[(?:v)?([^\]]+)\](?:\s+-\s+.*)?$', re.MULTILINE)
    matches = list(heading.finditer(changelog))

    for index, match in enumerate(matches):
        if normalize_version(match.group(1)) != wanted:
            continue
        start = match.end()
        end = matches[index + 1].start() if index + 1 < len(matches) else len(changelog)
        return changelog[start:end].strip()

    raise SystemExit(f'No CHANGELOG.md section found for v{wanted}')


def normalize_section_titles(section):
    lines = []
    for line in section.splitlines():
        title = line.removeprefix('### ').strip() if line.startswith('### ') else None
        if title:
            line = f'## {SECTION_TITLE_MAP.get(title, title)}'
        lines.append(line)
    return '\n'.join(lines).strip()


def last_sunday(year, month):
    day = datetime(year, month + 1, 1, tzinfo=timezone.utc) - timedelta(days=1)
    return day - timedelta(days=(day.weekday() + 1) % 7)


def crossink_local_time():
    now_utc = datetime.now(timezone.utc)
    year = now_utc.year
    cest_start = last_sunday(year, 3).replace(hour=1, minute=0, second=0, microsecond=0)
    cest_end = last_sunday(year, 10).replace(hour=1, minute=0, second=0, microsecond=0)
    if cest_start <= now_utc < cest_end:
        return now_utc + timedelta(hours=2), 'CEST'
    return now_utc + timedelta(hours=1), 'CET'


def default_intro(version):
    now, timezone_name = crossink_local_time()
    time_text = now.strftime('%I:%M%p').lower()
    date_text = f"{now.strftime('%B')} {now.day}, {now.year}"
    return (
        f'This fork release syncs HenryBaby/CrossInk with upstream uxjulia/CrossInk '
        f'release/v{version} as of {time_text} {timezone_name} {date_text}. '
        'Unless explicitly marked as fork-specific, the changes listed below come from upstream CrossInk.'
    )


def parse_args():
    parser = argparse.ArgumentParser(description='Generate release notes from CHANGELOG.md.')
    parser.add_argument('--version', required=True, help='Release version, with or without leading v.')
    parser.add_argument('--changelog', default='CHANGELOG.md', type=Path, help='Path to CHANGELOG.md.')
    parser.add_argument('--output', required=True, type=Path, help='Output markdown file.')
    parser.add_argument('--intro', default=None, help='Optional release intro line without the leading blockquote marker.')
    return parser.parse_args()


def main():
    args = parse_args()
    version = normalize_version(args.version)
    changelog = args.changelog.read_text(encoding='utf-8')
    section = normalize_section_titles(extract_version_section(changelog, version))
    intro = args.intro.strip() if args.intro else default_intro(version)

    body = f"""> {intro}

{section}

{TIP}"""

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(body.strip() + '\n', encoding='utf-8')
    print(f'Release notes written to: {args.output}')


if __name__ == '__main__':
    main()
