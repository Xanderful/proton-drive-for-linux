---
description: Create a patch for the WebClients submodule.
---

1. Navigate to the WebClients directory:
   ```bash
   cd WebClients
   ```

2. Make your code changes in the WebClients files.

3. Generate the patch:
   ```bash
   # Replace 'descriptive-name.patch' with a real name
   git diff > ../patches/common/descriptive-name.patch
   ```

4. Verify the patch content:
   ```bash
   cat ../patches/common/descriptive-name.patch
   ```

5. Return to root and rebuild web components:
   ```bash
   cd ..
   scripts/build-webclients.sh
   # or
   make build-web
   ```
