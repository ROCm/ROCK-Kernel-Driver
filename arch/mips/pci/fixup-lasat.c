#include <linux/init.h>
#include <linux/pci.h>

void __init pcibios_fixup_irqs(void)
{
}

struct pci_fixup pcibios_fixups[] __initdata = {
    { 0 }
};
