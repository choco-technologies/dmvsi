# dmvsi Documentation

dmvsi is the interface of the converters into dmview views: source formats
(HTML, ...) are plugins (dmf modules implementing the dmvsi DIF), programs
that make views convert files through the dmvsi API and get documents.

## Contents

- **[api-reference.md](api-reference.md)** - the API for programs and plugins, and the DIF
- **[writing-a-converter.md](writing-a-converter.md)** - a converter plugin, step by step

View documentation using `dmf-man`:

```bash
dmf-man dmvsi          # Main documentation
dmf-man dmvsi api      # API reference
```
