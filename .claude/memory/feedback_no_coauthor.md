---
name: No Co-Author in commits
description: User does not want Co-Authored-By lines in git commit messages
type: feedback
---

Do not add `Co-Authored-By: Claude ...` trailer to commit messages.

**Why:** User explicitly said "I don't like that" when seeing the co-author line.

**How to apply:** When creating git commits, omit the Co-Authored-By trailer entirely.
