# Publishing checklist

This project intentionally has no relationship to either historical Git history.
Do not copy the old `.git` directory or force-push this project over an existing
history.

## Before the first commit

1. Revoke both OAuth values found during the legacy review.
2. Review `README.md`, `LICENSE`, `SECURITY.md`, and `THIRD_PARTY_NOTICES.md`.
3. Build with warnings as errors and run CTest.
4. Confirm `.env` and generated output are ignored.
5. Scan tracked and untracked publication candidates for OAuth values, token
   assignments, private environment files, and generated artifacts:

   ```powershell
   python scripts/check_release_safety.py
   ```

6. Confirm no historical artifact type is staged:

   ```powershell
   git status --short
   git diff --cached --check
   git ls-files
   ```

Only documented placeholders are acceptable. The scanner redacts values from its
output; never add an exception for a real token.

## Create and publish

Create an empty GitHub repository without generated README, license, or `.gitignore`
files. Then run from this folder:

```powershell
git init -b main
git add .
git commit -m "Publish reviewed C++ Twitch chatbot reconstruction"
git remote add origin https://github.com/<owner>/Twitch-Chatbot-CPP.git
git push -u origin main
```

Wait for all four CI areas to pass: Windows, GCC/Clang on Linux, release safety,
and the container build/configuration check. Inspect the rendered README and documentation links before optionally
archiving the repository in **Settings -> General -> Archive this repository**.
