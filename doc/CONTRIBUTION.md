Contribution
------------

This is my personal project, but if you want to contribute - feel free!

How to contribute
=================

### Reporting issues
Found a bug or have an idea? Open an issue.

### Pull requests
1. Fork the repository
2. Create a feature/bugfix branch (`feat/stuff` or `fix/thing`)
3. Make your changes
4. Open a PR with a clear description

No strict bureaucracy; just make sure it compiles, works, and doesn't break everything.

Releases
========

Releases happen when they happen. 

Version format: `Maj.Min`  
Changelog format:

```
Maj.Min -> "Cool word"
----------------------
 * List of changes
 * Added XRandR for multi-monitor support

```

# Code style

Keep it simple. All control flow structures and functions follow a single pattern:

```
keyword (statement) {
    instructions
}
```

K&R style for short.

This applies to `if`, `while`, `for`, `switch`, and functions.

Single-instruction bodies can be written as one-liners without braces:

```
keyword (statement) instruction;
```
