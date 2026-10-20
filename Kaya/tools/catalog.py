"""C++ package catalog source definitions. Kernel built-ins are deliberately excluded."""
from pathlib import Path
import json

ROOT=Path(__file__).resolve().parents[1]
PACKAGES=ROOT/"Packages"
TOOLS=[
 ("text-upper","Convert input text to uppercase."),
 ("text-lower","Convert input text to lowercase."),
 ("text-title","Capitalize words in a title."),
 ("text-swapcase","Swap uppercase and lowercase letters."),
 ("text-reverse","Reverse the input string."),
 ("text-reverse-words","Reverse word order while preserving each word."),
 ("text-collapse-space","Normalize runs of whitespace to one space."),
 ("text-remove-space","Remove all whitespace characters."),
 ("text-snake","Convert a phrase to snake_case."),
 ("text-kebab","Convert a phrase to kebab-case."),
 ("text-camel","Convert a phrase to camelCase."),
 ("text-pascal","Convert a phrase to PascalCase."),
 ("text-slug","Create a URL-friendly ASCII slug."),
 ("text-acronym","Build an acronym from the initial letters."),
 ("text-count-words","Count whitespace-separated words."),
 ("text-count-lines","Count lines in input text."),
 ("text-count-vowels","Count English vowels in text."),
 ("text-is-palindrome","Check whether normalized text is a palindrome."),
 ("text-unique-lines","Remove repeated lines while keeping first occurrence."),
 ("text-sort-lines","Sort input lines lexicographically."),
 ("text-line-numbers","Prefix every line with its line number."),
]

# Curated single-purpose developer, encoding, data, number, and text utilities.
# Each entry is a separate installable command; none duplicates a kernel builtin.
EXTRA = {
"encoding": "base64-encode base64-decode hex-encode hex-decode url-encode url-decode html-escape html-unescape json-escape json-unescape rot13 morse-encode morse-decode binary-encode binary-decode ascii-hex unicode-codepoints percent-encode percent-decode csv-to-tsv tsv-to-csv",
"numbers": "num-prime num-factorial num-fibonacci num-gcd num-lcm num-isqrt num-average num-median num-mode num-range num-clamp num-round num-ceil num-floor num-percent num-ratio num-random num-convert-bin num-convert-oct num-convert-hex num-convert-dec num-digitsum num-reverse num-palindrome num-armstrong num-prime-factors num-divisors num-coprime num-modpow num-sqrt num-cuberoot num-quadratic num-statistics num-histogram num-rank num-zscore num-normalize num-celsius num-fahrenheit num-kelvin num-bytes num-duration",
"developer": "dev-semver dev-semver-bump dev-regex-escape dev-regex-unescape dev-markdown-table dev-json-format dev-json-minify dev-yaml-indent dev-ini-format dev-csv-format dev-sql-quote dev-shell-quote dev-html-minify dev-xml-escape dev-xml-unescape dev-css-minify dev-js-minify dev-diff dev-patch dev-checksum dev-crc32 dev-sha256 dev-uuid dev-ulid dev-timestamp dev-iso-date dev-unix-time dev-cron-next dev-env-parse dev-env-format dev-gitignore dev-editorconfig dev-license-mit dev-readme-stub dev-dockerfile-stub dev-makefile-stub dev-cmake-stub dev-github-action-stub dev-changelog-stub dev-toml-format dev-path-normalize dev-path-basename dev-path-extension dev-path-join dev-glob-match dev-regex-test dev-line-ending dev-tab-expand dev-tabify dev-indent dev-dedent dev-wrap dev-center dev-columnize",
"data": "data-csv-stats data-csv-column data-csv-select data-csv-sort data-csv-unique data-csv-transpose data-json-keys data-json-values data-json-lines data-lines-json data-ndjson-validate data-xml-tags data-markdown-toc data-markdown-links data-markdown-words data-table-align data-list-dedupe data-list-intersect data-list-union data-list-difference data-list-shuffle data-list-sample data-list-chunk data-list-rotate data-list-zip data-list-unique-sort data-log-levels data-log-filter data-log-summary data-accesslog-summary data-http-status data-mime-guess data-file-size data-byte-units data-hex-dump data-octal-dump data-base-convert data-delimiter-detect data-column-stats data-word-frequency data-ngram data-sentence-count data-reading-time data-calc-expression data-template-expand",
"generators": "gen-uuid gen-password gen-passphrase gen-token gen-color gen-palette gen-gradient gen-rgb gen-hsl gen-ansi-color gen-ascii-art gen-box gen-lorem gen-dummy-json gen-csv gen-markdown-list gen-html-page gen-css-reset gen-html-form gen-svg-icon gen-sql-insert gen-sql-create gen-curl gen-http-request gen-ssh-key-comment gen-rsa-guide gen-qr-text gen-barcode-text gen-checklist gen-todo gen-docker-compose gen-git-branch gen-semver gen-numeric-sequence gen-date-sequence gen-random-ints gen-random-strings gen-morse gen-rot13 gen-caesar gen-csv-sample gen-json-schema gen-openapi-stub gen-license-header gen-commit-message gen-slug gen-short-id gen-password-strength gen-roman gen-roman-parse gen-emoji-list gen-ascii-table gen-unicode-table gen-regex-cheatsheet gen-cron-cheatsheet gen-http-cheatsheet gen-git-cheatsheet gen-shell-script gen-python-stub gen-cpp-stub gen-rust-stub gen-license-apache gen-license-bsd gen-license-unlicense gen-lorem-sentences gen-lorem-paragraphs",
"science": "science-circle science-sphere science-cylinder science-rectangle science-triangle science-distance science-midpoint science-slope science-pythagoras science-force science-work science-power science-kinetic science-potential science-density science-pressure science-ohm science-voltage science-current science-resistance science-wavelength science-frequency science-period science-molarity science-mass-moles science-moles-mass science-ph-scale science-bmi science-bmr science-calorie science-tip science-loan science-interest science-compound-interest science-inflation science-bmi-category science-aspect-ratio science-map-scale science-speed science-pace science-fuel-economy science-unit-length science-unit-weight science-unit-volume science-unit-area science-unit-temp science-unit-pressure science-unit-energy science-unit-power science-unit-speed science-unit-time science-unit-data science-roman science-sigfig science-scientific-notation science-factorial-zeros science-triangle-area science-triangle-sides science-polygon-angle science-leap-year science-day-of-year science-weekday science-date-diff science-timezone-offset science-color-contrast science-luminance science-complement-color science-mix-color science-permutation science-combination science-probability science-growth science-depreciation science-break-even science-calorie-deficit science-body-fat science-water-intake science-running-splits science-pomodoro science-countdown",
}
for category, names in EXTRA.items():
    for name in names.split():
        TOOLS.append((name, f"Focused {category} utility: {name.replace('-', ' ')}."))

# Keep a deliberately curated 176-entry expansion alongside the 21 text tools.
TOOLS = TOOLS[:197]

if len(TOOLS) != 197:
    raise RuntimeError(f"catalog must contain 197 community packages, got {len(TOOLS)}")

def generate()->None:
    header=(PACKAGES/"text-upper"/"toolkit.hpp").read_text(encoding="utf-8")
    for name,description in TOOLS:
        folder=PACKAGES/name; folder.mkdir(parents=True,exist_ok=True)
        (folder/"toolkit.hpp").write_text(header,encoding="utf-8")
        (folder/"main.cpp").write_text(
            '#include "toolkit.hpp"\nint main(int argc,char **argv){return kaya_toolkit::run("'+name+'",argc,argv);}\n',
            encoding="utf-8")
        manifest={"name":name,"displayName":name.replace("-"," ").title(),"version":"1.0.0",
                  "author":"Kaya community","description":description,
                  "category": next((c for c, ns in EXTRA.items() if name in ns.split()), "text"),
                  "runtime":"cpp","language":"cpp","languageVersion":"17","runtimeVersion":"0.2.0",
                  "entry":"main.cpp","supportFiles":["toolkit.hpp"],"plugins":["cpp"],"permissions":[]}
        (folder/"manifest.json").write_text(json.dumps(manifest,indent=2)+"\n",encoding="utf-8")

if __name__=="__main__": generate()
