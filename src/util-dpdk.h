/* Copyright (C) 2020 Igor Mjasojedov
 *
 * You can copy, redistribute or modify this Program under the terms of
 * the GNU General Public License version 2 as published by the Free
 * Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * version 2 along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 */

/**
 * \file
 *
 * \author Igor Mjasojedov <mjasojedov.igor13@gmail.com>
 */

#ifndef __UTIL_DPDK_H__
#define __UTIL_DPDK_H__

#include "tm-threads-common.h"

#ifdef HAVE_DPDK

int DpdkPortInit(int port, struct rte_mempool *mbuf_pool, uint16_t nb_queues);

#endif /* HAVE_DPDK */
#endif /* __UTIL_DPDK_H__ */