extern atomic_t chanq_tasks;
extern dasd_chanq_t *cq_head;

cqr_t *request_cqr (int, int);
int release_cqr (cqr_t *);
int dasd_chanq_enq (dasd_chanq_t *, cqr_t *);
int dasd_chanq_deq (dasd_chanq_t *, cqr_t *);
void cql_enq_head (dasd_chanq_t * q);
void cql_deq (dasd_chanq_t * q);
