/*
 * Copyright (C) 2012, Nils Asmussen <nils@os.inf.tu-dresden.de>
 * Economic rights: Technische Universitaet Dresden (Germany)
 *
 * This file is part of NRE (NOVA runtime environment).
 *
 * NRE is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * NRE is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License version 2 for more details.
 */

#pragma once

#include <arch/Types.h>
#include <Exception.h>
#include <mem/DataSpaceDesc.h>
#include <util/MaskField.h>
#include <util/SList.h>
#include <util/Math.h>
#include <Assert.h>

extern void test_reglist();

namespace nre {

class ChildMemoryException : public Exception {
public:
	explicit ChildMemoryException(ErrorCode code) throw() : Exception(code) {
	}
};

class OStream;
class ChildMemory;
OStream &operator<<(OStream &os,const ChildMemory &cm);

/**
 * Manages the virtual memory of a child process
 */
class ChildMemory {
	friend void ::test_reglist();
	friend OStream &operator<<(OStream &os,const ChildMemory &cm);

public:
	enum Perm {
		R	= DataSpaceDesc::R,
		W	= DataSpaceDesc::W,
		X	= DataSpaceDesc::X,
		RW	= R | W,
		RX	= R | X,
		RWX	= R | W | X,
	};

	/**
	 * A dataspace in the address space of the child including administrative information.
	 */
	class DS : public SListItem {
	public:
		/**
		 * Creates the dataspace with given descriptor and cap
		 */
		explicit DS(const DataSpaceDesc &desc,capsel_t cap)
			: SListItem(), _desc(desc), _cap(cap),
			  _perms(Math::blockcount<size_t>(desc.size(),ExecEnv::PAGE_SIZE) * 4) {
		}

		/**
		 * @return the permission masks for all pages
		 */
		const MaskField<4> &perms() const {
			return _perms;
		}
		/**
		 * @return the dataspace descriptor
		 */
		DataSpaceDesc &desc() {
			return _desc;
		}
		/**
		 * @return the dataspace descriptor
		 */
		const DataSpaceDesc &desc() const {
			return _desc;
		}
		/**
		 * @return the dataspace (unmap) capability
		 */
		capsel_t cap() const {
			return _cap;
		}
		/**
		 * @param addr the virtual address (is expected to be in this dataspace)
		 * @return the origin for the given address
		 */
		uintptr_t origin(uintptr_t addr) const {
			return _desc.origin() + (addr - _desc.virt());
		}
		/**
		 * @param addr the virtual address (is expected to be in this dataspace)
		 * @return the permissions of the given page
		 */
		uint page_perms(uintptr_t addr) const {
			return _perms.get((addr - _desc.virt()) / ExecEnv::PAGE_SIZE);
		}
		/**
		 * Sets the permissions of the given page range to <perms>. It sets all pages until it
		 * encounters a page that already has the given permissions. Additionally, it makes sure
		 * not to leave this dataspace.
		 *
		 * @param addr the virtual address where to start
		 * @param pages the number of pages
		 * @param perms the permissions to set
		 * @return the actual number of pages that have been changed
		 */
		size_t page_perms(uintptr_t addr,size_t pages,uint perms) {
			uintptr_t off = addr - _desc.virt();
			pages = Math::min<size_t>(pages,(_desc.size() - off) / ExecEnv::PAGE_SIZE);
			for(size_t i = 0, o = off / ExecEnv::PAGE_SIZE; i < pages; ++i, ++o) {
				uint oldperms = _perms.get(o);
				if(oldperms == perms)
					return i;
				_perms.set(o,perms);
			}
			return pages;
		}
		/**
		 * Sets the permissions of all pages to <perms>
		 *
		 * @param perms the new permissions
		 */
		void all_perms(uint perms) {
			_perms.set_all(perms);
		}

	private:
		DataSpaceDesc _desc;
		capsel_t _cap;
		MaskField<4> _perms;
	};

	typedef SListIterator<DS> iterator;

	/**
	 * Constructor
	 */
	explicit ChildMemory() : _list() {
	}

	/**
	 * @return the first dataspace
	 */
	iterator begin() const {
		return _list.begin();
	}
	/**
	 * @return end of dataspaces
	 */
	iterator end() const {
		return _list.end();
	}

	/**
	 * Finds the dataspace with given selector
	 *
	 * @param sel the selector
	 * @return the dataspace or 0 if not found
	 */
	DS *find(capsel_t sel) {
		return get(sel);
	}
	/**
	 * Finds the dataspace with given address
	 *
	 * @param addr the virtual address
	 * @return the dataspace or 0 if not found
	 */
	DS *find_by_addr(uintptr_t addr) {
		for(iterator it = begin(); it != end(); ++it) {
			if(addr >= it->desc().virt() && addr < it->desc().virt() + it->desc().size())
				return &*it;
		}
		return 0;
	}

	/**
	 * Finds a free position in the address space to put in <size> bytes.
	 *
	 * @param size the number of bytes to map
	 * @return the address where it can be mapped
	 * @throw ChildMemoryException if there is not enough space
	 */
	uintptr_t find_free(size_t size) const {
		// find the end of the "highest" region
		uintptr_t e = 0;
		for(iterator it = begin(); it != end(); ++it) {
			if(it->desc().virt() + it->desc().size() > e)
				e = it->desc().virt() + it->desc().size();
		}
		// leave one page space (earlier error detection)
		e = (e + ExecEnv::PAGE_SIZE * 2 - 1) & ~(ExecEnv::PAGE_SIZE - 1);
		// check if the size fits below the kernel
		if(e + size < e || e + size > ExecEnv::KERNEL_START)
			throw ChildMemoryException(E_CAPACITY);
		return e;
	}

	/**
	 * Adds the given dataspace to the address space
	 *
	 * @param desc the dataspace descriptor (desc.virt() is expected to contain the address where
	 * 	the memory is located in the parent (=us))
	 * @param addr the virtual address where to map it to in the child
	 * @param perm the permissions to use (desc.perm() is ignored)
	 * @param sel the selector for the dataspace
	 */
	void add(const DataSpaceDesc& desc,uintptr_t addr,uint perm,capsel_t sel) {
		DS *ds = new DS(DataSpaceDesc(desc.size(),desc.type(),perm,desc.phys(),addr,desc.virt()),sel);
		_list.append(ds);
	}

	/**
	 * Removes the dataspace with given selector
	 *
	 * @param sel the selector
	 */
	void remove(capsel_t sel) {
		remove(get(sel));
	}

	/**
	 * Removes the dataspace that contains the given address
	 *
	 * @param addr the virtual address
	 */
	void remove_by_addr(uintptr_t addr) {
		remove(find_by_addr(addr));
	}

private:
	DS *get(capsel_t sel) {
		for(iterator it = begin(); it != end(); ++it) {
			if(it->cap() == sel)
				return &*it;
		}
		return 0;
	}
	void remove(DS *ds) {
		if(!ds)
			throw ChildMemoryException(E_NOT_FOUND);
		_list.remove(ds);
		delete ds;
	}

	SList<DS> _list;
};

}
