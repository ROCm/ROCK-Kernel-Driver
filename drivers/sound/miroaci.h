extern int aci_implied_cmd(unsigned char opcode);
extern int aci_write_cmd(unsigned char opcode, unsigned char parameter);
extern int aci_write_cmd_d(unsigned char opcode, unsigned char parameter, unsigned char parameter2);
extern int aci_read_cmd(unsigned char opcode, int length, unsigned char *parameter);
extern int aci_indexed_cmd(unsigned char opcode, unsigned char index, unsigned char *parameter);
