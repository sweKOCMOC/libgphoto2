/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* gphoto2-port-usb.c
 *
 * Copyright � 2001 Lutz M�ller <lutz@users.sf.net>
 * Copyright � 1999-2000 Johannes Erdfelt <johannes@erdfelt.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful, 
 * but WITHOUT ANY WARRANTY; without even the implied warranty of 
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details. 
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */
#include "config.h"
#include <gphoto2/gphoto2-port-library.h>

#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/time.h>
#include <sys/param.h>
#include <dirent.h>
#include <string.h>

#include <usb.h>

#include <gphoto2/gphoto2-port.h>
#include <gphoto2/gphoto2-port-result.h>
#include <gphoto2/gphoto2-port-log.h>

#ifdef ENABLE_NLS
#  include <libintl.h>
#  undef _
#  define _(String) dgettext (GETTEXT_PACKAGE, String)
#  ifdef gettext_noop
#    define N_(String) gettext_noop (String)
#  else
#    define N_(String) (String)
#  endif
#else
#  define textdomain(String) (String)
#  define gettext(String) (String)
#  define dgettext(Domain,Message) (Message)
#  define dcgettext(Domain,Message,Type) (Message)
#  define bindtextdomain(Domain,Directory) (Domain)
#  define _(String) (String)
#  define N_(String) (String)
#endif

#define CHECK(result) {int r=(result); if (r<0) return (r);}

struct _GPPortPrivateLibrary {
	void *dh;
	struct usb_device *d;

	int config;
	int interface;
	int altsetting;
};

GPPortType
gp_port_library_type (void)
{
	return (GP_PORT_USB);
}

int
gp_port_library_list (GPPortInfoList *list)
{
	GPPortInfo info;
	struct usb_bus *bus;
	struct usb_device *dev;
	int nrofdevices = 0, i, i1, i2, unknownint;

	/* default port first */
	info.type = GP_PORT_USB;
	strcpy (info.name, "Universal Serial Bus");
	strcpy (info.path, "usb:");
	CHECK (gp_port_info_list_append (list, info));

	/* generic matcher. This will catch passed XXX,YYY entries for instance. */
	memset (info.name, 0, sizeof(info.name));
	strcpy (info.path, "^usb:");
	CHECK (gp_port_info_list_append (list, info));

	usb_init ();
	usb_find_busses ();
	usb_find_devices ();

	strcpy (info.name, "Universal Serial Bus");

	bus = usb_get_busses();

	/* Look and enumerate all USB ports. */
	while (bus) {
		for (dev = bus->devices; dev; dev = dev->next) {
			/* Devices which are definitely not cameras. */
			if (	(dev->descriptor.bDeviceClass == USB_CLASS_HUB)		||
				(dev->descriptor.bDeviceClass == USB_CLASS_HID)		||
				(dev->descriptor.bDeviceClass == USB_CLASS_PRINTER)	||
				(dev->descriptor.bDeviceClass == USB_CLASS_COMM)
			)
				continue;
			/* excepts HUBs, usually the interfaces have the classes, not
			 * the device */
			unknownint = 0;
			for (i = 0; i < dev->descriptor.bNumConfigurations; i++) {
				if (!dev->config) {
					unknownint++;
					continue;
				}
				for (i1 = 0; i1 < dev->config[i].bNumInterfaces; i1++)
					for (i2 = 0; i2 < dev->config[i].interface[i1].num_altsetting; i2++) {
						struct usb_interface_descriptor *intf = &dev->config[i].interface[i1].altsetting[i2]; 
						if (	(intf->bInterfaceClass == USB_CLASS_HID)	||
							(intf->bInterfaceClass == USB_CLASS_PRINTER)	||
							(intf->bInterfaceClass == USB_CLASS_COMM))
							continue;
						unknownint++;
					}
			}
			/* when we find only hids, printer or comm ifaces  ... skip this */
			if (!unknownint)
				continue;
			/* Note: We do not skip USB storage. Some devices can support both,
			 * and the Ricoh erronously reports it.
			 */ 
			nrofdevices++;
		}
		bus = bus->next;
	}

	/* If we already added usb:, and have 0 or 1 devices we have nothing to do.
	 * This should be the standard use case.
	 */
	if (nrofdevices <= 1) 
		return (GP_OK);

	/* Redo the same bus/device walk, but now add the ports with usb:x,y notation,
	 * so we can address all USB devices.
	 */
	bus = usb_get_busses();
	while (bus) {
		for (dev = bus->devices; dev; dev = dev->next) {
			char *s;
			/* Devices which are definitely not cameras. */
			if (	(dev->descriptor.bDeviceClass == USB_CLASS_HUB)		||
				(dev->descriptor.bDeviceClass == USB_CLASS_HID)		||
				(dev->descriptor.bDeviceClass == USB_CLASS_PRINTER)	||
				(dev->descriptor.bDeviceClass == USB_CLASS_COMM)
			)
				continue;
			/* excepts HUBs, usually the interfaces have the classes, not
			 * the device */
			unknownint = 0;
			for (i = 0; i < dev->descriptor.bNumConfigurations; i++) {
				if (!dev->config) {
					unknownint++;
					continue;
				}
				for (i1 = 0; i1 < dev->config[i].bNumInterfaces; i1++)
					for (i2 = 0; i2 < dev->config[i].interface[i1].num_altsetting; i2++) {
						struct usb_interface_descriptor *intf = &dev->config[i].interface[i1].altsetting[i2]; 
						if (	(intf->bInterfaceClass == USB_CLASS_HID)	||
							(intf->bInterfaceClass == USB_CLASS_PRINTER)	||
							(intf->bInterfaceClass == USB_CLASS_COMM))
							continue;
						unknownint++;
					}
			}
			/* when we find only hids, printer or comm ifaces  ... skip this */
			if (!unknownint)
				continue;
			/* Note: We do not skip USB storage. Some devices can support both,
			 * and the Ricoh erronously reports it.
			 */ 
			sprintf (info.path, "usb:%s,%s", bus->dirname, dev->filename);
			/* On MacOS X we might get usb:006,002-04a9-3139-00-00. */
			s = strchr(info.path, '-');if (s) *s='\0';
			CHECK (gp_port_info_list_append (list, info));
		}
		bus = bus->next;
	}
	return (GP_OK);
}

static int gp_port_usb_init (GPPort *port)
{
	port->pl = malloc (sizeof (GPPortPrivateLibrary));
	if (!port->pl)
		return (GP_ERROR_NO_MEMORY);
	memset (port->pl, 0, sizeof (GPPortPrivateLibrary));

	port->pl->config = port->pl->interface = port->pl->altsetting = -1;

	usb_init ();
	usb_find_busses ();
	usb_find_devices ();

	return (GP_OK);
}

static int
gp_port_usb_exit (GPPort *port)
{
	if (port->pl) {
		free (port->pl);
		port->pl = NULL;
	}

	return (GP_OK);
}

static int
gp_port_usb_open (GPPort *port)
{
	int ret;
	char name[64];

	gp_log (GP_LOG_DEBUG,"libusb","gp_port_usb_open()");
	if (!port || !port->pl->d)
		return GP_ERROR_BAD_PARAMETERS;

        /*
	 * Open the device using the previous usb_handle returned by
	 * find_device
	 */
	port->pl->dh = usb_open (port->pl->d);
	if (!port->pl->dh) {
		gp_port_set_error (port, _("Could not open USB device (%m)."));
		return GP_ERROR_IO;
	}
#if defined(LIBUSB_HAS_GET_DRIVER_NP) && defined(LIBUSB_HAS_DETACH_KERNEL_DRIVER_NP)
	memset(name,0,sizeof(name));
	ret = usb_get_driver_np (port->pl->dh, port->settings.usb.interface,
		name, sizeof(name)
	);
	if (strstr(name,"usbfs")) {
		/* other gphoto instance most likely */
		gp_port_set_error (port, _("Camera is already in use."));
		return GP_ERROR_IO_LOCK;
	}

	if (ret >= 0) {
		gp_log (GP_LOG_DEBUG,"libusb",_("Device has driver '%s' attached, detaching it now."), name);
		ret = usb_detach_kernel_driver_np (port->pl->dh, port->settings.usb.interface);
		if (ret < 0)
			gp_port_set_error (port, _("Could not detach kernel driver '%s' of camera device."),name);
	} else {
		if (errno != ENODATA) /* ENODATA - just no driver there */
			gp_port_set_error (port, _("Could not query kernel driver of device."));
	}
#endif

	gp_log (GP_LOG_DEBUG,"libusb","claiming interface %d", port->settings.usb.interface);
	ret = usb_claim_interface (port->pl->dh,
				   port->settings.usb.interface);
	if (ret < 0) {
		gp_port_set_error (port, _("Could not claim "
			"interface %d (%m). Make sure no other program "
			"or kernel module (such as %s) is using the device "
			"and you have read/write access to the device."),
			port->settings.usb.interface, "sdc2xx, stv680, spca50x");
		return GP_ERROR_IO_USB_CLAIM;
	}
	return GP_OK;
}

static int
gp_port_usb_close (GPPort *port)
{
	if (!port || !port->pl->dh)
		return GP_ERROR_BAD_PARAMETERS;

	if (usb_release_interface (port->pl->dh,
				   port->settings.usb.interface) < 0) {
		gp_port_set_error (port, _("Could not "
			"release interface %d (%m)."),
			port->settings.usb.interface);
		return (GP_ERROR_IO);
	}

	/* This is only for our very special Canon cameras which need a good
	 * whack after close, otherwise they get timeouts on reconnect.
	 */
	if (port->pl->d->descriptor.idVendor == 0x04a9) {
		if (usb_reset (port->pl->dh) < 0) {
			gp_port_set_error (port, _("Could not reset USB port (%m)."));
			return (GP_ERROR_IO);
		}
	}

	if (usb_close (port->pl->dh) < 0) {
		gp_port_set_error (port, _("Could not close USB port (%m)."));
		return (GP_ERROR_IO);
	}

	port->pl->dh = NULL;

	return GP_OK;
}

static int
gp_port_usb_clear_halt_lib(GPPort *port, int ep)
{
	int ret=0;

	if (!port || !port->pl->dh)
		return GP_ERROR_BAD_PARAMETERS;

	switch (ep) {
	case GP_PORT_USB_ENDPOINT_IN :
		ret=usb_clear_halt(port->pl->dh, port->settings.usb.inep);
		break;
	case GP_PORT_USB_ENDPOINT_OUT :
		ret=usb_clear_halt(port->pl->dh, port->settings.usb.outep);
		break;
	case GP_PORT_USB_ENDPOINT_INT :
		ret=usb_clear_halt(port->pl->dh, port->settings.usb.intep);
		break;
	default:
		gp_port_set_error (port, "gp_port_usb_clear_halt: "
				   "bad EndPoint argument");
		return GP_ERROR_BAD_PARAMETERS;
	}
	return (ret ? GP_ERROR_IO_USB_CLEAR_HALT : GP_OK);
}

static int
gp_port_usb_write (GPPort *port, const char *bytes, int size)
{
        int ret;

	if (!port || !port->pl->dh)
		return GP_ERROR_BAD_PARAMETERS;

	ret = usb_bulk_write (port->pl->dh, port->settings.usb.outep,
                           (char *) bytes, size, port->timeout);
        if (ret < 0)
		return (GP_ERROR_IO_WRITE);

        return (ret);
}

static int
gp_port_usb_read(GPPort *port, char *bytes, int size)
{
	int ret;

	if (!port || !port->pl->dh)
		return GP_ERROR_BAD_PARAMETERS;

	ret = usb_bulk_read(port->pl->dh, port->settings.usb.inep,
			     bytes, size, port->timeout);
        if (ret < 0)
		return GP_ERROR_IO_READ;

        return ret;
}

static int
gp_port_usb_check_int (GPPort *port, char *bytes, int size, int timeout)
{
	int ret;

	if (!port || !port->pl->dh || timeout < 0)
		return GP_ERROR_BAD_PARAMETERS;

	ret = usb_interrupt_read(port->pl->dh, port->settings.usb.intep,
			     bytes, size, timeout);
        if (ret < 0)
		return GP_ERROR_IO_READ;

        return ret;
}

static int
gp_port_usb_msg_write_lib(GPPort *port, int request, int value, int index,
	char *bytes, int size)
{
	if (!port || !port->pl->dh)
		return GP_ERROR_BAD_PARAMETERS;

	return usb_control_msg(port->pl->dh,
		USB_TYPE_VENDOR | USB_RECIP_DEVICE,
		request, value, index, bytes, size, port->timeout);
}

static int
gp_port_usb_msg_read_lib(GPPort *port, int request, int value, int index,
	char *bytes, int size)
{
	if (!port || !port->pl->dh)
		return GP_ERROR_BAD_PARAMETERS;

	return usb_control_msg(port->pl->dh,
		USB_TYPE_VENDOR | USB_RECIP_DEVICE | USB_ENDPOINT_IN,
		request, value, index, bytes, size, port->timeout);
}

/* The next two functions support the nonstandard request types 0x41 (write) 
 * and 0xc1 (read), which are occasionally needed. 
 */

static int
gp_port_usb_msg_interface_write_lib(GPPort *port, int request, 
	int value, int index, char *bytes, int size)
{
	if (!port || !port->pl->dh)
		return GP_ERROR_BAD_PARAMETERS;

	return usb_control_msg(port->pl->dh, 
		USB_TYPE_VENDOR | USB_RECIP_INTERFACE,
		request, value, index, bytes, size, port->timeout);
}


static int
gp_port_usb_msg_interface_read_lib(GPPort *port, int request, 
	int value, int index, char *bytes, int size)
{
	if (!port || !port->pl->dh)
		return GP_ERROR_BAD_PARAMETERS;

	return usb_control_msg(port->pl->dh, 
		USB_TYPE_VENDOR | USB_RECIP_INTERFACE | USB_ENDPOINT_IN,
		request, value, index, bytes, size, port->timeout);
}


/* The next two functions support the nonstandard request types 0x21 (write) 
 * and 0xa1 (read), which are occasionally needed. 
 */

static int
gp_port_usb_msg_class_write_lib(GPPort *port, int request, 
	int value, int index, char *bytes, int size)
{
	if (!port || !port->pl->dh)
		return GP_ERROR_BAD_PARAMETERS;

	return usb_control_msg(port->pl->dh, 
		USB_TYPE_CLASS | USB_RECIP_INTERFACE,
		request, value, index, bytes, size, port->timeout);
}


static int
gp_port_usb_msg_class_read_lib(GPPort *port, int request, 
	int value, int index, char *bytes, int size)
{
	if (!port || !port->pl->dh)
		return GP_ERROR_BAD_PARAMETERS;

	return usb_control_msg(port->pl->dh, 
		USB_TYPE_CLASS | USB_RECIP_INTERFACE | USB_ENDPOINT_IN,
		request, value, index, bytes, size, port->timeout);
}

/*
 * This function applys changes to the device.
 *
 * New settings are in port->settings_pending and the old ones
 * are in port->settings. Compare them first and only call
 * usb_set_configuration() and usb_set_altinterface() if needed
 * since some USB devices does not like it if this is done
 * more than necessary (Canon Digital IXUS 300 for one).
 *
 */
static int
gp_port_usb_update (GPPort *port)
{
	int ret, ifacereleased = FALSE;

	gp_log (GP_LOG_DEBUG, "libusb", "gp_port_usb_update(old int=%d, conf=%d, alt=%d), (new int=%d, conf=%d, alt=%d)",
		port->settings.usb.interface,
		port->settings.usb.config,
		port->settings.usb.altsetting,
		port->settings_pending.usb.interface,
		port->settings_pending.usb.config,
		port->settings_pending.usb.altsetting
	);
	if (!port)
		return GP_ERROR_BAD_PARAMETERS;

	if (port->pl->interface == -1) port->pl->interface = port->settings.usb.interface;
	if (port->pl->config == -1) port->pl->config = port->settings.usb.config;
	if (port->pl->altsetting == -1) port->pl->altsetting = port->settings.usb.altsetting;

	/* The portname can also be changed with the device still fully closed. */
	memcpy(&port->settings.usb.port, &port->settings_pending.usb.port,
		sizeof(port->settings.usb.port));

 	if (!port->pl->dh)
		return GP_ERROR_BAD_PARAMETERS;

	memcpy(&port->settings.usb, &port->settings_pending.usb,
		sizeof(port->settings.usb));

	/* The interface changed. release the old, claim the new ... */
	if (port->settings.usb.interface != port->pl->interface) {
		gp_log (GP_LOG_DEBUG, "libusb", "changing interface %d -> %d",
			port->pl->interface,
			port->settings.usb.interface
		);
		if (usb_release_interface (port->pl->dh,
					   port->pl->interface) < 0) {
			gp_log (GP_LOG_DEBUG, "gphoto2-port-usb","releasing the iface for config failed.");
			/* Not a hard error for now. -Marcus */
		} else {
			gp_log (GP_LOG_DEBUG,"libusb","claiming interface %d", port->settings.usb.interface);
			ret = usb_claim_interface (port->pl->dh,
						   port->settings.usb.interface);
			if (ret < 0) {
				gp_log (GP_LOG_DEBUG, "gphoto2-port-usb","reclaiming the iface for config failed.");
				return GP_ERROR_IO_UPDATE;
			}
			port->pl->interface = port->settings.usb.interface;
		}
	}
	if (port->settings.usb.config != port->pl->config) {
		gp_log (GP_LOG_DEBUG, "libusb", "changing config %d -> %d",
			port->pl->config,
			port->settings.usb.config
		);
		/* This can only be changed with the interface released. 
		 * This is a hard requirement since 2.6.12.
		 */
		if (usb_release_interface (port->pl->dh,
					   port->settings.usb.interface) < 0) {
			gp_log (GP_LOG_DEBUG, "gphoto2-port-usb","releasing the iface for config failed.");
			ifacereleased = FALSE;
		} else {
			ifacereleased = TRUE;
		}
		ret = usb_set_configuration(port->pl->dh,
				     port->settings.usb.config);
		if (ret < 0) {
#if 0 /* setting the configuration failure is not fatal */
			gp_port_set_error (port,
				_("Could not set config %d/%d (%m)"),
				port->settings.usb.interface,
				port->settings.usb.config);
			return GP_ERROR_IO_UPDATE;	
#endif
			gp_log (GP_LOG_ERROR, "gphoto2-port-usb","setting configuration from %d to %d failed with ret = %d, but continue...", port->pl->config, port->settings.usb.config, ret);
		}

		gp_log (GP_LOG_DEBUG, "gphoto2-port-usb",
			"Changed usb.config from %d to %d",
			port->pl->config,
			port->settings.usb.config);

		if (ifacereleased) {
			gp_log (GP_LOG_DEBUG,"libusb","claiming interface %d", port->settings.usb.interface);
			ret = usb_claim_interface (port->pl->dh,
						   port->settings.usb.interface);
			if (ret < 0) {
				gp_log (GP_LOG_DEBUG, "gphoto2-port-usb","reclaiming the iface for config failed.");
			}
		}
		/*
		 * Copy at once if something else fails so that this
		 * does not get re-applied
		 */
		port->pl->config = port->settings.usb.config;
	}

	/* This can be changed with interface claimed. (And I think it must be claimed.) */
	if (port->settings.usb.altsetting != port->pl->altsetting) {
		ret = usb_set_altinterface(port->pl->dh, port->settings.usb.altsetting);
		if (ret < 0) {
			gp_port_set_error (port, 
				_("Could not set altsetting from %d "
				"to %d (%m)"),
				port->pl->altsetting,
				port->settings.usb.altsetting);
			return GP_ERROR_IO_UPDATE;
		}

		gp_log (GP_LOG_DEBUG, "gphoto2-port-usb",
			"Changed usb.altsetting from %d to %d",
			port->pl->altsetting,
			port->settings.usb.altsetting);
		port->pl->altsetting = port->settings.usb.altsetting;
	}

	return GP_OK;
}

static int
gp_port_usb_find_ep(struct usb_device *dev, int config, int interface, int altsetting, int direction, int type)
{
	struct usb_interface_descriptor *intf;
	int i;

	if (!dev->config)
		return -1;

	intf = &dev->config[config].interface[interface].altsetting[altsetting];

	for (i = 0; i < intf->bNumEndpoints; i++) {
		if ((intf->endpoint[i].bEndpointAddress & USB_ENDPOINT_DIR_MASK) == direction &&
		    (intf->endpoint[i].bmAttributes & USB_ENDPOINT_TYPE_MASK) == type)
			return intf->endpoint[i].bEndpointAddress;
	}

	return -1;
}

static int
gp_port_usb_find_first_altsetting(struct usb_device *dev, int *config, int *interface, int *altsetting)
{
	int i, i1, i2;

	if (!dev->config)
		return -1;

	for (i = 0; i < dev->descriptor.bNumConfigurations; i++)
		for (i1 = 0; i1 < dev->config[i].bNumInterfaces; i1++)
			for (i2 = 0; i2 < dev->config[i].interface[i1].num_altsetting; i2++)
				if (dev->config[i].interface[i1].altsetting[i2].bNumEndpoints) {
					*config = i;
					*interface = i1;
					*altsetting = i2;

					return 0;
				}

	return -1;
}

static int
gp_port_usb_find_device_lib(GPPort *port, int idvendor, int idproduct)
{
	struct usb_bus *bus;
	struct usb_device *dev;
	char *s;
	char busname[64], devname[64];

	if (!port)
		return (GP_ERROR_BAD_PARAMETERS);

	s = strchr (port->settings.usb.port,':');
	busname[0] = devname[0] = '\0';
	if (s && (s[1] != '\0')) { /* usb:%d,%d */
		strncpy(busname,s+1,sizeof(busname));
		busname[sizeof(busname)-1] = '\0';

		s = strchr(busname,',');
		if (s) {
			strncpy(devname, s+1,sizeof(devname));
			devname[sizeof(devname)-1] = '\0';
			*s = '\0';
		} else {
			busname[0] = '\0';
		}
	}
	/*
	 * NULL idvendor is not valid.
	 * NULL idproduct is ok.
	 * Should the USB layer report that ? I don't know.
	 * Better to check here.
	 */
	if (!idvendor) {
		gp_port_set_error (port, _("The supplied vendor or product "
			"id (0x%x,0x%x) is not valid."), idvendor, idproduct);
		return GP_ERROR_BAD_PARAMETERS;
	}

	for (bus = usb_busses; bus; bus = bus->next) {
		if ((busname[0] != '\0') && strcmp(busname, bus->dirname))
			continue;

		for (dev = bus->devices; dev; dev = dev->next) {
			if (	(devname[0] != '\0') &&
				(dev->filename != strstr(dev->filename, devname))
			)
				continue;

			if ((dev->descriptor.idVendor == idvendor) &&
			    (dev->descriptor.idProduct == idproduct)) {
				int config = -1, interface = -1, altsetting = -1;

				port->pl->d = dev;

				gp_log (GP_LOG_VERBOSE, "gphoto2-port-usb",
					"Looking for USB device "
					"(vendor 0x%x, product 0x%x)... found.", 
					idvendor, idproduct);

				/* Use the first config, interface and altsetting we find */
				gp_port_usb_find_first_altsetting(dev, &config, &interface, &altsetting);

				/* Set the defaults */
				if (dev->config) {
					int i;

					if (dev->config[config].interface[interface].altsetting[altsetting].bInterfaceClass
					    == USB_CLASS_MASS_STORAGE) {
						gp_log (GP_LOG_VERBOSE, "gphoto2-port-usb",
							_("USB device (vendor 0x%x, product 0x%x) is a mass"
							  " storage device, and might not function with gphoto2."
							  " Reference: %s"),
							idvendor, idproduct, URL_USB_MASSSTORAGE);
					}
					port->settings.usb.config = dev->config[config].bConfigurationValue;
					port->settings.usb.interface = dev->config[config].interface[interface].altsetting[altsetting].bInterfaceNumber;
					port->settings.usb.altsetting = dev->config[config].interface[interface].altsetting[altsetting].bAlternateSetting;

					port->settings.usb.inep = gp_port_usb_find_ep(dev, config, interface, altsetting, USB_ENDPOINT_IN, USB_ENDPOINT_TYPE_BULK);
					port->settings.usb.outep = gp_port_usb_find_ep(dev, config, interface, altsetting, USB_ENDPOINT_OUT, USB_ENDPOINT_TYPE_BULK);
					port->settings.usb.intep = gp_port_usb_find_ep(dev, config, interface, altsetting, USB_ENDPOINT_IN, USB_ENDPOINT_TYPE_INTERRUPT);

					port->settings.usb.maxpacketsize = 0;
					gp_log (GP_LOG_DEBUG, "gphoto2-port-usb", "inep to look for is %02x", port->settings.usb.inep);
					for (i=0;i<dev->config[config].interface[interface].altsetting[altsetting].bNumEndpoints;i++) {
						if (port->settings.usb.inep == dev->config[config].interface[interface].altsetting[altsetting].endpoint[i].bEndpointAddress) {
							port->settings.usb.maxpacketsize = dev->config[config].interface[interface].altsetting[altsetting].endpoint[i].wMaxPacketSize;
							break;
						}
					}
					gp_log (GP_LOG_VERBOSE, "gphoto2-port-usb",
						"Detected defaults: config %d, "
						"interface %d, altsetting %d, "
						"inep %02x, outep %02x, intep %02x, "
						"class %02x, subclass %02x",
						port->settings.usb.config,
						port->settings.usb.interface,
						port->settings.usb.altsetting,
						port->settings.usb.inep,
						port->settings.usb.outep,
						port->settings.usb.intep,
						dev->config[config].interface[interface].altsetting[altsetting].bInterfaceClass,
						dev->config[config].interface[interface].altsetting[altsetting].bInterfaceSubClass
						);
				}

				return GP_OK;
			}
		}
	}

	gp_port_set_error (port, _("Could not find USB device "
		"(vendor 0x%x, product 0x%x). Make sure this device "
		"is connected to the computer."), idvendor, idproduct);
	return GP_ERROR_IO_USB_FIND;
}

/* This function reads the Microsoft OS Descriptor and looks inside to
 * find if it is a MTP device. This is the similar to the way that
 * Windows Media Player 10 uses.
 * It is documented to some degree on various internet pages.
 */
static int
gp_port_usb_match_mtp_device(struct usb_device *dev,int *configno, int *interfaceno, int *altsettingno)
{
	char buf[1000], cmd;
	int ret,i,i1,i2;
	usb_dev_handle *devh;

	/* All of them are "vendor specific" device class */
#if 0
	if ((dev->descriptor.bDeviceClass!=0xff) && (dev->descriptor.bDeviceClass!=0))
		return 0;
#endif

	devh = usb_open (dev);
	/* get string descriptor at 0xEE */
	ret = usb_get_descriptor (devh, 0x03, 0xee, buf, sizeof(buf));
	if (ret > 0) gp_log_data("get_MS_OSD",buf, ret);
	if (ret < 10) goto errout;
	if (!((buf[2] == 'M') && (buf[4]=='S') && (buf[6]=='F') && (buf[8]=='T')))
		goto errout;
	cmd = buf[16];
	ret = usb_control_msg (devh, USB_ENDPOINT_IN|USB_RECIP_DEVICE|USB_TYPE_VENDOR, cmd, 0, 4, buf, sizeof(buf), 1000);
	if (ret == -1) {
		gp_log (GP_LOG_ERROR, "mtp matcher", "control message says %d\n", ret);
		goto errout;
	}
	if (buf[0] != 0x28) {
		gp_log (GP_LOG_ERROR, "mtp matcher", "ret is %d, buf[0] is %x\n", ret, buf[0]);
		goto errout;
	}
	if (ret > 0) gp_log_data("get_MS_ExtDesc",buf, ret);
	if ((buf[0x12] != 'M') || (buf[0x13] != 'T') || (buf[0x14] != 'P')) {
		gp_log (GP_LOG_ERROR, "mtp matcher", "buf at 0x12 is %02x%02x%02x\n", buf[0x12], buf[0x13], buf[0x14]);
		goto errout;
	}
	ret = usb_control_msg (devh, USB_ENDPOINT_IN|USB_RECIP_DEVICE|USB_TYPE_VENDOR, cmd, 0, 5, buf, sizeof(buf), 1000);
	if (ret == -1) goto errout;
	if (buf[0] != 0x28) {
		gp_log (GP_LOG_ERROR, "mtp matcher", "ret is %d, buf[0] is %x\n", ret, buf[0]);
		goto errout;
	}
	if (ret > 0) gp_log_data("get_MS_ExtProp",buf, ret);
	if ((buf[0x12] != 'M') || (buf[0x13] != 'T') || (buf[0x14] != 'P')) {
		gp_log (GP_LOG_ERROR, "mtp matcher", "buf at 0x12 is %02x%02x%02x\n", buf[0x12], buf[0x13], buf[0x14]);
		goto errout;
	}
	usb_close (devh);

	/* Now chose a nice interface for us to use ... Just take the first. */

	if (dev->descriptor.bNumConfigurations > 1)
		gp_log (GP_LOG_ERROR, "mtp matcher", "The device has %d configurations!\n", dev->descriptor.bNumConfigurations);
	for (i = 0; i < dev->descriptor.bNumConfigurations; i++) {
		struct usb_config_descriptor *config =
			&dev->config[i];

		if (config->bNumInterfaces > 1)
			gp_log (GP_LOG_ERROR, "mtp matcher", "The configuration has %d interfaces!\n", config->bNumInterfaces);
		for (i1 = 0; i1 < config->bNumInterfaces; i1++) {
			struct usb_interface *interface =
				&config->interface[i1];

			if (interface->num_altsetting > 1)
				gp_log (GP_LOG_ERROR, "mtp matcher", "The interface has %d altsettings!\n", interface->num_altsetting);
			for (i2 = 0; i2 < interface->num_altsetting; i2++) {
				*configno = i;
				*interfaceno = i1;
				*altsettingno = i2;
				return 1;
			}
		}
	}
	return 1;
errout:
	usb_close (devh);
	return 0;
}

static int
gp_port_usb_match_device_by_class(struct usb_device *dev, int class, int subclass, int protocol, int *configno, int *interfaceno, int *altsettingno)
{
	int i, i1, i2;

	if (class == 666) /* Special hack for MTP devices with MS OS descriptors. */
		return gp_port_usb_match_mtp_device (dev, configno, interfaceno, altsettingno);

	if (dev->descriptor.bDeviceClass == class &&
	    (subclass == -1 ||
	     dev->descriptor.bDeviceSubClass == subclass) &&
	    (protocol == -1 ||
	     dev->descriptor.bDeviceProtocol == protocol))
		return 1;

	if (!dev->config)
		return 0;

	for (i = 0; i < dev->descriptor.bNumConfigurations; i++) {
		struct usb_config_descriptor *config =
			&dev->config[i];

		for (i1 = 0; i1 < config->bNumInterfaces; i1++) {
			struct usb_interface *interface =
				&config->interface[i1];

			for (i2 = 0; i2 < interface->num_altsetting; i2++) {
				struct usb_interface_descriptor *altsetting =
					&interface->altsetting[i2];

				if (altsetting->bInterfaceClass == class &&
				    (subclass == -1 ||
				     altsetting->bInterfaceSubClass == subclass) &&
				    (protocol == -1 ||
				     altsetting->bInterfaceProtocol == protocol)) {
					*configno = i;
					*interfaceno = i1;
					*altsettingno = i2;

					return 2;
				}
			}
		}
	}

	return 0;
}

static int
gp_port_usb_find_device_by_class_lib(GPPort *port, int class, int subclass, int protocol)
{
	struct usb_bus *bus;
	struct usb_device *dev;
	char *s;
	char busname[64], devname[64];

	if (!port)
		return (GP_ERROR_BAD_PARAMETERS);

	busname[0] = devname[0] = '\0';
	s = strchr (port->settings.usb.port,':');
	if (s && (s[1] != '\0')) { /* usb:%d,%d */
		strncpy(busname,s+1,sizeof(busname));
		busname[sizeof(busname)-1] = '\0';

		s = strchr(busname,',');
		if (s) {
			strncpy(devname, s+1,sizeof(devname));
			devname[sizeof(devname)-1] = '\0';
			*s = '\0';
		} else {
			busname[0] = '\0';
		}
	}
	/*
	 * NULL class is not valid.
	 * NULL subclass and protocol is ok.
	 * Should the USB layer report that ? I don't know.
	 * Better to check here.
	 */
	if (!class)
		return GP_ERROR_BAD_PARAMETERS;

	for (bus = usb_busses; bus; bus = bus->next) {
		if ((busname[0] != '\0') && strcmp(busname, bus->dirname))
			continue;

		for (dev = bus->devices; dev; dev = dev->next) {
			int ret, config = -1, interface = -1, altsetting = -1;

			if ((devname[0] != '\0') && strcmp(devname, dev->filename))
				continue;

			ret = gp_port_usb_match_device_by_class(dev, class, subclass, protocol, &config, &interface, &altsetting);
			if (!ret)
				continue;

			port->pl->d = dev;
			gp_log (GP_LOG_VERBOSE, "gphoto2-port-usb",
				"Looking for USB device "
				"(class 0x%x, subclass, 0x%x, protocol 0x%x)... found.", 
				class, subclass, protocol);
			/* Set the defaults */
			if (dev->config) {
				int i;

				port->settings.usb.config = dev->config[config].bConfigurationValue;
				port->settings.usb.interface = dev->config[config].interface[interface].altsetting[altsetting].bInterfaceNumber;
				port->settings.usb.altsetting = dev->config[config].interface[interface].altsetting[altsetting].bAlternateSetting;

				port->settings.usb.inep = gp_port_usb_find_ep(dev, config, interface, altsetting, USB_ENDPOINT_IN, USB_ENDPOINT_TYPE_BULK);
				port->settings.usb.outep = gp_port_usb_find_ep(dev, config, interface, altsetting, USB_ENDPOINT_OUT, USB_ENDPOINT_TYPE_BULK);
				port->settings.usb.intep = gp_port_usb_find_ep(dev, config, interface, altsetting, USB_ENDPOINT_IN, USB_ENDPOINT_TYPE_INTERRUPT);
				port->settings.usb.maxpacketsize = 0;
				gp_log (GP_LOG_DEBUG, "gphoto2-port-usb", "inep to look for is %02x", port->settings.usb.inep);
				for (i=0;i<dev->config[config].interface[interface].altsetting[altsetting].bNumEndpoints;i++) {
					if (port->settings.usb.inep == dev->config[config].interface[interface].altsetting[altsetting].endpoint[i].bEndpointAddress) {
						port->settings.usb.maxpacketsize = dev->config[config].interface[interface].altsetting[altsetting].endpoint[i].wMaxPacketSize;
						break;
					}
				}
				gp_log (GP_LOG_VERBOSE, "gphoto2-port-usb",
					"Detected defaults: config %d, "
					"interface %d, altsetting %d, "
					"idVendor ID %04x, idProduct %04x, "
					"inep %02x, outep %02x, intep %02x",
					port->settings.usb.config,
					port->settings.usb.interface,
					port->settings.usb.altsetting,
					dev->descriptor.idVendor,
					dev->descriptor.idProduct,
					port->settings.usb.inep,
					port->settings.usb.outep,
					port->settings.usb.intep
					);
			}

			return GP_OK;
		}
	}

	gp_port_set_error (port, _("Could not find USB device "
		"(class 0x%x, subclass 0x%x, protocol 0x%x). Make sure this device "
		"is connected to the computer."), class, subclass, protocol);
	return GP_ERROR_IO_USB_FIND;
}

GPPortOperations *
gp_port_library_operations (void)
{
	GPPortOperations *ops;

	ops = malloc (sizeof (GPPortOperations));
	if (!ops)
		return (NULL);
	memset (ops, 0, sizeof (GPPortOperations));

	ops->init   = gp_port_usb_init;
	ops->exit   = gp_port_usb_exit;
	ops->open   = gp_port_usb_open;
	ops->close  = gp_port_usb_close;
	ops->read   = gp_port_usb_read;
	ops->write  = gp_port_usb_write;
	ops->check_int = gp_port_usb_check_int;
	ops->update = gp_port_usb_update;
	ops->clear_halt = gp_port_usb_clear_halt_lib;
	ops->msg_write  = gp_port_usb_msg_write_lib;
	ops->msg_read   = gp_port_usb_msg_read_lib;
	ops->msg_interface_write  = gp_port_usb_msg_interface_write_lib;
	ops->msg_interface_read   = gp_port_usb_msg_interface_read_lib;
	ops->msg_class_write  = gp_port_usb_msg_class_write_lib;
	ops->msg_class_read   = gp_port_usb_msg_class_read_lib;
	ops->find_device = gp_port_usb_find_device_lib;
	ops->find_device_by_class = gp_port_usb_find_device_by_class_lib;

	return (ops);
}
