#!/bin/sh
# Make a release of the version in resources/Info.plist.
# Usage: scripts/release.sh
# It runs the tests and the replay of Chapter 1 on this Mac, the tests on the
# phone, builds the .deb, pushes
# the commits and the tag, publishes the GitHub release with the notes of
# CHANGELOG.md, adds the .deb to the Cydia repository in ../cydia, and
# closes the milestone of the version with its issues.
set -e

ROOT=$(cd "$(dirname "$0")/.." && pwd)
CYDIA=${CYDIA_REPO:-$ROOT/../cydia}
REPO=Stiven-Gjekaj/Alpharune
VERSION=$(plutil -extract CFBundleShortVersionString raw "$ROOT/resources/Info.plist")
TAG="v$VERSION"
DEB="io.github.stivengjekaj.alpharune_${VERSION}_iphoneos-arm.deb"
cd "$ROOT"

[ -z "$(git status --porcelain)" ] || { echo "release: commit the changes first" >&2; exit 1; }
grep -q "^## $VERSION " CHANGELOG.md || { echo "release: CHANGELOG.md has no entry for $VERSION" >&2; exit 1; }
if git rev-parse -q --verify "refs/tags/$TAG" >/dev/null; then
    echo "release: the tag $TAG is already there" >&2
    exit 1
fi

scripts/test.sh >build/release-tests.log || { cat build/release-tests.log >&2; exit 1; }
tail -1 build/release-tests.log
scripts/replay.sh
scripts/test.sh --phone >build/release-tests.log || { cat build/release-tests.log >&2; exit 1; }
tail -1 build/release-tests.log
scripts/deb.sh >/dev/null

git push -q
git tag -a "$TAG" -m "Alpharune $VERSION"
git push -q origin "$TAG"
awk -v v="## $VERSION " 'index($0, v) == 1 {f = 1; next} /^## / {f = 0} f' CHANGELOG.md >build/notes.md
cat >>build/notes.md <<EOF

Install: copy the .deb to the phone and run \`dpkg -i $DEB\` as root. Copy
\`game.ios\`, \`options.ini\`, \`chapter1_mac/\`, and \`mus/\` from your own copy
of DELTARUNE to \`/var/mobile/Media/Alpharune/\`. The .deb holds no file of
DELTARUNE.
EOF
gh release create "$TAG" -R "$REPO" -t "Alpharune $VERSION" -F build/notes.md "build/$DEB"

# The Cydia repository: add the package, and show the new version.
(
    cd "$CYDIA"
    scripts/update.sh "$ROOT/build/$DEB"
    # update.sh also makes the page of the repository again.
    git add debs Packages Packages.bz2 Packages.gz Release index.html $(ls -d icons 2>/dev/null)
    git commit -qm "feat: add Alpharune $VERSION"
    export ROW="| Alpharune (\`io.github.stivengjekaj.alpharune\`) | $VERSION | iOS 6 or newer |"
    if grep -q '^| Alpharune (' README.md; then
        sed -i '' "s/^| Alpharune (\`io.github.stivengjekaj.alpharune\`) | [0-9.]* |/| Alpharune (\`io.github.stivengjekaj.alpharune\`) | $VERSION |/" README.md
    else
        # The first release adds a row under the last row of the table.
        perl -0pi -e 's/((?:^\| \S+ \(`io\.github[^\n]*\n)+)/$1$ENV{ROW}\n/m' README.md
    fi
    git add README.md
    git commit -qm "docs: list Alpharune $VERSION"
    git push -q
)

# Close the issues of the milestone, then the milestone.
MILESTONE=$(gh api "repos/$REPO/milestones?state=open" -q ".[] | select(.title | startswith(\"$TAG\")) | .number")
if [ -n "$MILESTONE" ]; then
    TITLE=$(gh api "repos/$REPO/milestones/$MILESTONE" -q .title)
    for issue in $(gh issue list -R "$REPO" -m "$TITLE" --json number -q '.[].number'); do
        gh issue close "$issue" -R "$REPO" -c "Done in $TAG." >/dev/null
    done
    gh api -X PATCH "repos/$REPO/milestones/$MILESTONE" -f state=closed -q '.title + ": " + .state'
fi
echo "released $TAG"
