#pragma once

// Initialize button GPIO and start monitoring task.
// Short press (<500ms): currently unused
// 3-second hold (3000–4999ms): session setup AP override toggle
//   - In Recovery Mode: exit recovery, disconnect STA, stay AP-only (keep creds)
//   - In AP-only with creds: re-engage Recovery Mode
//   - In AP-only without creds: no-op
// 5-second hold (≥5000ms): factory reset
void button_init(void);
