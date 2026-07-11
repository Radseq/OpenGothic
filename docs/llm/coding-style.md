# Coding Style — Full Client MMO Adapter

- Follow existing OpenGothic style and C++23 workspace requirements.
- Keep bridge functions small and separate input capture from presentation.
- Avoid networking work on render/UI threads.
- Use stable IDs/generations, not display names or object addresses, for MMO
  identity.
- Use bounded drains and explicit overflow/fault handling.
- Preserve native paths; MMO branching should be explicit and localized.
- Prefer client_sandbox/public facade types over duplicated packet builders.
