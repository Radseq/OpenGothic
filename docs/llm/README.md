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
14. `ai/28-mmo-db-server-ai-authority-foundation-step116.md`
15. `ai/29-mmo-observed-npc-authority-feed-step117.md`
16. `ai/30-mmo-server-bound-fresh-new-game-step118.md`
17. `ai/31-mmo-db-continue-npc-routine-wakeup-step119.md`
18. `ai/32-mmo-db-continue-npc-authority-apply-step120.md`
19. `ai/33-mmo-server-parity-state-bridge-step121.md`
20. `ai/33-mmo-npc-observation-failopen-item-refresh-guard-step122.md`
21. `ai/34-mmo-clean-script-applies-step122-step123.md`
22. `ai/35-mmo-menu-db-character-identity-step124.md`
23. `ai/36-mmo-default-character-menu-step125.md`
24. `ai/37-mmo-server-authority-roadmap-step126.md`
25. `ai/38-mmo-server-world-clock-module-step127.md`
26. `ai/39-mmo-server-movement-authority-module-step128.md`
27. `ai/40-mmo-server-persistence-bridge-module-step129.md`
28. `ai/41-mmo-server-persistence-cli-adapter-step130.md`
29. `ai/42-mmo-server-persistence-result-adapter-step131.md`
30. `ai/43-mmo-server-persistence-session-adapter-step132.md`
31. `ai/44-mmo-server-persistence-authority-ops-step133.md`
32. `ai/45-mmo-server-persistence-direct-ops-step134.md`
33. `ai/46-mmo-server-persistence-bootstrap-slices-step135.md`
34. `ai/47-mmo-server-persistence-bootstrap-character-world-slices-step136.md`
35. `ai/48-mmo-server-persistence-positioned-bootstrap-slices-step137.md`
36. `ai/49-mmo-server-persistence-direct-apply-simple-ops-step138.md`
37. `ai/50-mmo-server-persistence-item-ops-step139.md`
38. `ai/51-mmo-server-persistence-module-split-step140.md`
39. `ai/52-mmo-server-quest-authority-step141.md`
40. `ai/53-mmo-server-dialog-authority-step142.md`
41. `ai/54-mmo-server-script-authority-step143.md`
42. `ai/55-mmo-server-waypoint-authority-bootstrap-step144.md`
43. `ai/56-mmo-server-waypoint-graph-step145.md`
44. `ai/57-mmo-server-npc-actions-dialog-sync-step146.md`
45. `ai/58-mmo-server-conversation-authority-step147.md`
46. `ai/59-mmo-server-npc-activity-registry-step148.md`
47. `ai/60-mmo-server-npc-action-priority-step149.md`
48. `ai/61-mmo-server-combat-outcome-policy-step150.md`
49. `ai/62-mmo-server-ai-client-parity-audit-step151.md`
50. `ai/63-mmo-server-combat-swing-timeline-audit-step152.md`
51. `ai/64-mmo-server-combat-timeline-registry-step153.md`
52. `ai/65-mmo-server-combat-animation-observation-step154.md`

Do not load old numbered step files during normal work. They are archaeology.
Keep their hard facts only when they became durable project rules below.

Target: Gothic II NotR first, later Gothic 1/Gothic 2 vanilla.

Durable engineering requirements:
- C++23.
- Use `constexpr` where it is meaningful and keeps code clear.
- Maximum runtime performance is a priority, especially on server hot paths.
- Code must be safe, explicit, readable and production-shaped.
- Build a solid base for a future Gothic MMO and server-authoritative gameplay.
- Write senior-level C++: relatively small functions, clear module boundaries,
  separation of concerns and replaceable components.
- Old single-player behavior must remain unchanged unless an explicit
  MMO/server flag is passed.
- When moving client/gameplay logic to the server, first inspect how the client
  and Gothic scripts currently make the decision. Do not invent a parallel
  interpretation. Recreate the same semantics in server-authoritative form,
  adjusted only where the server architecture requires it.
