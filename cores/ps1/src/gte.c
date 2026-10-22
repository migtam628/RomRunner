/* GTE (COP2), the geometry coprocessor. Stage 1 keeps its register file so
 * MFC2/MTC2/LWC2/SWC2 round-trip; stage 3 adds the commands. */
#include "ps1.h"

uint32_t gte_read_data(PS1 *p, uint32_t r) { return p->gte_data[r & 31]; }
void gte_write_data(PS1 *p, uint32_t r, uint32_t v) { p->gte_data[r & 31] = v; }
uint32_t gte_read_ctrl(PS1 *p, uint32_t r) { return p->gte_ctrl[r & 31]; }
void gte_write_ctrl(PS1 *p, uint32_t r, uint32_t v) { p->gte_ctrl[r & 31] = v; }
void gte_command(PS1 *p, uint32_t op) { (void)p; (void)op; }
