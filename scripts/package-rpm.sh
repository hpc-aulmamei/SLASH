#!/bin/bash
set -euxo pipefail

cd "$(dirname "$0")/.."

VERSION="${VERSION:-0.0.1}"
TOPDIR="$(pwd)/rpmbuild"
ARTIFACTS_DIR="${ARTIFACTS_DIR:-$(pwd)/rpm}"

rm -rf "${TOPDIR}" "${ARTIFACTS_DIR}"
mkdir -p "${TOPDIR}"/{BUILD,RPMS,SOURCES,SPECS,SRPMS}
mkdir -p "${ARTIFACTS_DIR}"

# Create source tarball (rpmbuild expects name-version/ inside)
tar czf "${TOPDIR}/SOURCES/slash-${VERSION}.tar.gz" \
    --transform="s,^\.,slash-${VERSION}," \
    --exclude='.git' \
    --exclude='rpmbuild' \
    --exclude='rpm' \
    --exclude='deb' \
    --exclude='pbuild' \
    .

cp rpm/slash.spec "${TOPDIR}/SPECS/"

rpmbuild \
    --define "_topdir ${TOPDIR}" \
    --define "_version ${VERSION}" \
    -ba "${TOPDIR}/SPECS/slash.spec"

cp "${TOPDIR}"/RPMS/*/*.rpm "${ARTIFACTS_DIR}/"
cp "${TOPDIR}"/SRPMS/*.rpm  "${ARTIFACTS_DIR}/"

cd "${ARTIFACTS_DIR}"
createrepo .

echo "RPMs available in ${ARTIFACTS_DIR}/"
