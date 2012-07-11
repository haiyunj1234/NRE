/*
 * (c) 2012 Nils Asmussen <nils@os.inf.tu-dresden.de>
 *     economic rights: Technische Universität Dresden (Germany)
 *
 * This file is part of TUD:OS and distributed under the terms of the
 * GNU General Public License 2.
 * Please see the COPYING-GPL-2 file for details.
 */

#pragma once

#include <arch/ExecEnv.h>
#include <kobj/Ports.h>
#include <mem/DataSpace.h>
#include <services/Console.h>

#include "ConsoleSessionData.h"
#include "Screen.h"

class HostVGA : public Screen {
	enum Register {
		CURSOR_HI		= 0xa,
		CURSOR_LO		= 0xb,
		START_ADDR_HI	= 0xc,
		START_ADDR_LO	= 0xd,
		CURSOR_LOC_HI	= 0xe,
		CURSOR_LOC_LO	= 0xf
	};
	enum {
		VGA_MEM			= 0xa0000,
		VGA_PAGE_SIZE	= nre::ExecEnv::PAGE_SIZE,
	};

public:
	explicit HostVGA() : Screen(), _ports(0x3d4,2),
		_ds(VGA_PAGE_SIZE * PAGES,nre::DataSpaceDesc::ANONYMOUS,nre::DataSpaceDesc::RW,VGA_MEM) {
	}

	virtual nre::DataSpace &mem() {
		return _ds;
	}
	virtual void set_regs(const nre::Console::Register &regs);

private:
	void write(Register reg,uint8_t val) {
		_ports.out<uint8_t>(reg,0);
		_ports.out<uint8_t>(val,1);
	}

	nre::Ports _ports;
	nre::DataSpace _ds;
	nre::Console::Register _last;
};