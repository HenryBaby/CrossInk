#!/usr/bin/env python3
"""
Generate GitHub release notes from CHANGELOG.md and README.md.

The release workflow uses CHANGELOG.md for version-specific notes and README.md
for the persistent downstream-fork section.
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

DOWNSTREAM_SECTION_TITLE = 'Changes maintained by this downstream fork'
NON_PUBLISHED_SECTION_TITLES = {'Internal'}

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
        if line.startswith('#### '):
            title = line.removeprefix('#### ').strip()
            line = f'### {SECTION_TITLE_MAP.get(title, title)}'
        elif line.startswith('### '):
            title = line.removeprefix('### ').strip()
            line = f'## {SECTION_TITLE_MAP.get(title, title)}'
        lines.append(line)
    return '\n'.join(lines).strip()

def remove_non_published_sections(section):
    """Remove explicitly internal headings before publishing release notes."""
    lines = section.splitlines()
    kept = []
    index = 0
    while index < len(lines):
        line = lines[index]
        heading = re.match(r'^(#{2,6})\s+(.+?)\s*$', line)
        if heading and heading.group(2) in NON_PUBLISHED_SECTION_TITLES:
            level = len(heading.group(1))
            index += 1
            while index < len(lines):
                next_heading = re.match(r'^(#{1,6})\s+', lines[index])
                if next_heading and len(next_heading.group(1)) <= level:
                    break
                index += 1
            continue
        kept.append(line)
        index += 1
    return '\n'.join(kept).strip()


def extract_markdown_section(markdown, title):
    section = re.compile(
        rf'^## {re.escape(title)}\s*\n\s*\n(?P<bullets>(?:- [^\n]*(?:\n|$))+)',
        re.MULTILINE,
    )
    match = section.search(markdown)
    if not match:
        raise SystemExit(f'No README.md bullet section found for "{title}"')

    return f'## {title}\n\n{match.group("bullets").strip()}'


def remove_markdown_section(markdown, title):
    heading = re.compile(rf'^## {re.escape(title)}\s*$', re.MULTILINE)
    match = heading.search(markdown)
    if not match:
        return markdown.strip()

    next_heading = re.search(r'^## ', markdown[match.end():], re.MULTILINE)
    end = match.end() + next_heading.start() if next_heading else len(markdown)
    return (markdown[:match.start()] + markdown[end:]).strip()


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


def default_intro():
    now, timezone_name = crossink_local_time()
    time_text = now.strftime('%I:%M%p').lower()
    date_text = f"{now.strftime('%B')} {now.day}, {now.year}"
    return f'This release is up to date with the main branch of CrossInk as of {time_text} {timezone_name} {date_text}.'


def parse_args():
    parser = argparse.ArgumentParser(description='Generate release notes from CHANGELOG.md.')
    parser.add_argument('--version', required=True, help='Release version, with or without leading v.')
    parser.add_argument('--changelog', default='CHANGELOG.md', type=Path, help='Path to CHANGELOG.md.')
    parser.add_argument('--readme', default='README.md', type=Path, help='Path to README.md.')
    parser.add_argument('--output', required=True, type=Path, help='Output markdown file.')
    parser.add_argument('--intro', default=None, help='Optional release intro line without the leading blockquote marker.')
    return parser.parse_args()


def main():
    args = parse_args()
    version = normalize_version(args.version)
    changelog = args.changelog.read_text(encoding='utf-8')
    section = remove_non_published_sections(extract_version_section(changelog, version))
    section = normalize_section_titles(section)
    section = remove_markdown_section(section, DOWNSTREAM_SECTION_TITLE)
    downstream_section = extract_markdown_section(args.readme.read_text(encoding='utf-8'), DOWNSTREAM_SECTION_TITLE)
    intro = args.intro.strip() if args.intro else default_intro()

    body = f"""> {intro}

{section}

{downstream_section}"""

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(body.strip() + '\n', encoding='utf-8')
    print(f'Release notes written to: {args.output}')


if __name__ == '__main__':
    main()
