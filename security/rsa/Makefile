#rsacore-objs := rsa.o mpi.o mpi_generic.o

rsacore-objs :=						\
		gnupg_mpi_generic_mpih-lshift.o		\
		gnupg_mpi_generic_mpih-mul1.o		\
		gnupg_mpi_generic_mpih-mul2.o		\
		gnupg_mpi_generic_mpih-mul3.o		\
		gnupg_mpi_generic_mpih-rshift.o		\
		gnupg_mpi_generic_mpih-sub1.o		\
		gnupg_mpi_generic_mpih-add1.o		\
		gnupg_mpi_generic_udiv-w-sdiv.o		\
		gnupg_mpi_mpicoder.o			\
		gnupg_mpi_mpi-add.o			\
		gnupg_mpi_mpi-bit.o			\
		gnupg_mpi_mpi-div.o			\
		gnupg_mpi_mpi-cmp.o			\
		gnupg_mpi_mpi-gcd.o			\
		gnupg_mpi_mpih-cmp.o			\
		gnupg_mpi_mpih-div.o			\
		gnupg_mpi_mpih-mul.o			\
		gnupg_mpi_mpi-inline.o			\
		gnupg_mpi_mpi-inv.o			\
		gnupg_mpi_mpi-mpow.o			\
		gnupg_mpi_mpi-mul.o			\
		gnupg_mpi_mpi-pow.o			\
		gnupg_mpi_mpi-scan.o			\
		gnupg_mpi_mpiutil.o			\
		gnupg_cipher_rsa-verify.o		\
		rsa.o					\
		rsa_key.o

obj-$(CONFIG_SECURITY_RSA) += rsacore.o

