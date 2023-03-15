/**
 * @file pci.c
 * @author Joe Bayer (joexbayer)
 * @brief Reads and enumerates the PCI devices, registering them and attaching drivers.
 * @version 0.1
 * @date 2022-06-01
 * 
 * @copyright Copyright (c) 2022
 * 
 */

#include <pci.h>
#include <e1000.h>
#include <ata.h>
#include <terminal.h>
#include <serial.h>
#include <io.h>

static const char* pci_classes[] =
{
	[0x0] = "Unknown",
	[0x1] = "Storage controller",
	[0x2] = "Network controller",
	[0x3] = "Display controller",
	[0x4] = "Multimedia device",
	[0x5] = "Memory controller",
	[0x6] = "Bridge device",
    [0x7] = "Simple Communication Controller",
    [0x8] = "Base System Peripheral",
    [0x9] = "Input Device Controller",
    [0xA] = "Docking Station",
    [0xB] = "Processor",
    [0xC] = "Serial Bus Controller",
    [0xD] = "Wireless Controller",
    [0xE] = "Intelligent Controller",
    [0xF] = "Satellite Communication Controller",
    [0x10] = "Encryption Controller",
    [0x11] = "Signal Processing Controller"
};

struct pci_driver registered_drivers[] = {
    {(uint16_t)E1000_VENDOR_ID, (uint16_t)E1000_DEVICE_ID, &e1000_attach},
    {0x8086, 0x7010, &ata_ide_init},
    {0, 0, 0}
};

struct pci_device _pci_devices[8];
int _pci_devices_size = 0;

/* https://wiki.osdev.org/PCI */
uint16_t pci_read_word(uint16_t bus, uint16_t slot, uint16_t func, uint16_t offset)
{
	uint32_t address;
    uint32_t lbus = (uint32_t)bus;
    uint32_t lslot = (uint32_t)slot;
    uint32_t lfunc = (uint32_t)func;
    uint16_t tmp = 0;
    address = (uint32_t)((lbus << 16) | (lslot << 11) |
              (lfunc << 8) | (offset & 0xfc) | ((uint32_t)0x80000000));
    outportl (0xCF8, address);
    tmp = (uint16_t)((inportl (0xCFC) >> ((offset & 2) * 8)) & 0xffff);
    return (tmp);
}

void pci_write_dword(uint16_t bus, uint16_t slot, uint16_t func, uint16_t offset, uint32_t data)
{
	uint32_t address;
    uint32_t lbus = (uint32_t)bus;
    uint32_t lslot = (uint32_t)slot;
    uint32_t lfunc = (uint32_t)func;

    address = (uint32_t)((lbus << 16) | (lslot << 11) |
              (lfunc << 8) | (offset & 0xfc) | ((uint32_t)0x80000000));

    outportl (0xCF8, address);
    outportl(0xCFC, data);;
}

/* Inline functions for reading pci device registers */
inline uint16_t pci_get_device_class(uint16_t bus, uint16_t slot, uint16_t function)
{
    return (pci_read_word(bus,slot,function,0xA) & 0xFF00) >> 8;
}

inline uint8_t pci_get_device_irq(uint16_t bus, uint16_t slot, uint16_t func)
{
    return (uint8_t) (pci_read_word(bus, slot, func, 0x3C) & 0x00ff);
}

inline uint32_t pci_get_device_base32(uint16_t bus, uint16_t slot, uint16_t function)
{   
    return (uint32_t)pci_read_word(bus, slot, function, 0x12) << 16 | (uint32_t)pci_read_word(bus, slot, function, 0x10);
}

void pci_enable_device_busmaster(uint16_t bus, uint16_t slot, uint16_t function)
{
    uint16_t dev_command_reg = pci_read_word(bus, slot, function, 0x04);
    uint16_t dev_status_reg = pci_read_word(bus, slot, function, 0x06);
    dev_command_reg |= (1 << 2); /* enable busmaster */
    pci_write_dword(bus, slot, function, 0x04, (uint32_t)dev_status_reg << 16 | (uint32_t) dev_command_reg);
}

int pci_register_device(uint32_t bus, uint32_t slot, uint32_t function, uint16_t vendor, uint16_t device, uint16_t class, uint8_t irq, uint32_t base)
{

	struct pci_device pci_dev;

    pci_dev.device = device;
    pci_dev.vendor = vendor;
    pci_dev.bus = bus;
    pci_dev.slot = slot;
    pci_dev.function = function;
	pci_dev.class = class;
	pci_dev.irq = irq;
    pci_dev.base = base;

	_pci_devices[_pci_devices_size] = pci_dev;
	_pci_devices_size++;

    return _pci_devices_size-1;

}

void pci_init()
{
    int devices_found = 0;
    for(uint32_t bus = 0; bus < 256; bus++)
    {
        for(uint32_t slot = 0; slot < 32; slot++)
        {
            for(uint32_t function = 0; function < 8; function++)
            {
                    uint16_t vendor = pci_read_word(bus, slot, function, 0);
                    if(vendor == 0xffff) continue;

                    uint16_t device = pci_read_word(bus, slot, function, 2);
					uint16_t class = pci_get_device_class(bus, slot, function);
					uint8_t irq = pci_get_device_irq(bus, slot ,function);
                    uint32_t base = pci_get_device_base32(bus, slot, function);

					int driver_index = pci_register_device(bus, slot, function, vendor, device, class, irq, base);
                    dbgprintf("DEVICE: Vendor: 0x%x, Device: 0x%x - %s\n", 
                        vendor,
                        device,
                        class < 0x11 ? pci_classes[class] : pci_classes[0]
                    );
                    
                    devices_found++;

                    int i = 0;
                    for (struct pci_driver driver = registered_drivers[i]; driver.vendor != 0; driver = registered_drivers[i])
                    {
                        if(driver.vendor == vendor && driver.device == device)
                        {
                            driver.attach(&_pci_devices[driver_index]);
                        }
                        i++;
                    }
                    
            }
        }
    }
    dbgprintf("[PCI] Peripheral Component Interconnect devices probed.\n");
}

void list_pci_devices()
{
    for (int i = 0; i < _pci_devices_size; i++)
    {
        
    }   
}

uint8_t pci_find_device(uint16_t find_vendor, uint16_t find_device)
{
    for(uint32_t bus = 0; bus < 256; bus++)
    {
        for(uint32_t slot = 0; slot < 32; slot++)
        {
            for(uint32_t function = 0; function < 8; function++)
            {
                    uint16_t vendor = pci_read_word(bus, slot, function, 0);
                    if(vendor == 0xffff) continue;
                   	if(vendor != find_vendor) continue;

                    uint16_t device = pci_read_word(bus, slot, function, 2);
					if(device == find_device)
					{
						return 1;
					}
            }
        }
    }
    return 0;
}