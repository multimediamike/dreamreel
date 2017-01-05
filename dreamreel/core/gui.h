#ifndef _GUI_H_
#define _GUI_H_

typedef struct {
  cont_cond_t cont;
} io_status_t;

void init_elements(void);

void update_elements(io_status_t *status);

void draw_elements(void);

#endif
