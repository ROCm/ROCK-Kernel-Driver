void* cx22702_create(struct i2c_adapter *i2c,
		     int pll_addr, struct dvb_pll_desc *pll,
		     int demod_addr);
int cx22702_destroy(void*);
