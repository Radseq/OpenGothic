# OpenGothic MMO AI Context

Read these files by default, in order:

1. `ai/00-current-state.md`
2. `ai/01-authority-data-model.md`
3. `ai/02-gameplay-domains.md`
4. `ai/03-code-map-and-hooks.md`
5. `ai/04-cpp-asio-network.md`
6. `ai/05-dev-loop-and-next-work.md`
7. `ai/06-save-to-server-roadmap.md`
8. `ai/22-mmo-menu-db-continue-step109.md`
9. `ai/23-db-continue-no-new-game-trigger-step110.md`
10. `ai/24-mmo-db-continue-startup-suppression-step112.md`
11. `ai/25-mmo-menu-in-session-db-continue-step113.md`
12. `ai/26-mmo-db-mover-materialization-step114.md`
13. `ai/27-mmo-db-world-clock-routine-bootstrap-step115.md`

Do not load old numbered step files during normal work. They are archaeology.
Keep their hard facts only when they became durable project rules below.

Target: Gothic II NotR first, later Gothic 1/Gothic 2 vanilla. C++23. Prefer
safe, explicit, high-performance code. Old single-player behavior must remain
unchanged unless an explicit MMO/server flag is passed.
