#!/usr/bin/env bash
# One-time setup: creates the Android release keystore and stores it in the
# repository's Actions secrets so every release APK is signed with the same
# key (required for installing updates over an older version).
#
#   tool/android_signing.sh            # needs: keytool (JDK) and gh (logged in)
#
# Keep the generated romrunner-release.jks somewhere safe and OUT of the repo.
# If you lose it, existing installs must be uninstalled before updating.
set -euo pipefail

REPO="${REPO:-migtam628/RomRunner}"
KEYSTORE="${KEYSTORE:-$HOME/romrunner-release.jks}"
ALIAS=romrunner

command -v keytool >/dev/null || { echo "keytool not found (install a JDK)" >&2; exit 1; }
command -v gh >/dev/null || { echo "gh not found (https://cli.github.com)" >&2; exit 1; }

if [ -e "$KEYSTORE" ]; then
  echo "Using existing keystore $KEYSTORE"
  read -rsp "Keystore password: " PASS; echo
else
  read -rsp "Choose a keystore password (6+ chars): " PASS; echo
  keytool -genkeypair -v -keystore "$KEYSTORE" -alias "$ALIAS" \
    -keyalg RSA -keysize 4096 -validity 10000 \
    -storepass "$PASS" -keypass "$PASS" \
    -dname "CN=RomRunner, O=MT Tech Services LLC, C=US"
  echo "Created $KEYSTORE"
fi

base64 < "$KEYSTORE" | tr -d '\n' | gh secret set ANDROID_KEYSTORE_BASE64 --repo "$REPO"
printf '%s' "$PASS" | gh secret set ANDROID_KEYSTORE_PASSWORD --repo "$REPO"
printf '%s' "$PASS" | gh secret set ANDROID_KEY_PASSWORD --repo "$REPO"
printf '%s' "$ALIAS" | gh secret set ANDROID_KEY_ALIAS --repo "$REPO"
echo "Secrets set on $REPO. The next release APK will be signed with this key."
