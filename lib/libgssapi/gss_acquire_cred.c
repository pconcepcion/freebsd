/*-
 * Copyright (c) 2005 Doug Rabson
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	$FreeBSD$
 */

#include <gssapi/gssapi.h>
#include <stdlib.h>
#include <errno.h>

#include "mech_switch.h"
#include "name.h"
#include "cred.h"

OM_uint32
gss_acquire_cred(OM_uint32 *minor_status,
    const gss_name_t desired_name,
    OM_uint32 time_req,
    const gss_OID_set desired_mechs,
    gss_cred_usage_t cred_usage,
    gss_cred_id_t *output_cred_handle,
    gss_OID_set *actual_mechs,
    OM_uint32 *time_rec)
{
	OM_uint32 major_status;
	gss_OID_set mechs = desired_mechs;
	gss_OID_set_desc set;
	struct _gss_name *name = (struct _gss_name *) desired_name;
	struct _gss_mech_switch *m;
	struct _gss_cred *cred;
	struct _gss_mechanism_cred *mc;
	struct _gss_mechanism_name *mn;
	OM_uint32 min_time, time;
	int i;

	/*
	 * First make sure that at least one of the requested
	 * mechanisms is one that we support.
	 */
	if (mechs) {
		_gss_load_mech();
		for (i = 0; i < mechs->count; i++) {
			int t;
			gss_test_oid_set_member(minor_status,
			    &mechs->elements[i], _gss_mech_oids, &t);
			if (t)
				break;
		}
		if (i == mechs->count) {
			*output_cred_handle = 0;
			*minor_status = 0;
			return (GSS_S_BAD_MECH);
		}
	}

	if (actual_mechs) {
		major_status = gss_create_empty_oid_set(minor_status,
		    actual_mechs);
		if (major_status)
			return (major_status);
	}

	cred = malloc(sizeof(struct _gss_cred));
	if (!cred) {
		if (actual_mechs)
			gss_release_oid_set(minor_status, actual_mechs);
		*minor_status = ENOMEM;
		return (GSS_S_FAILURE);
	}
	cred->gc_usage = cred_usage;
	SLIST_INIT(&cred->gc_mc);

	if (mechs == GSS_C_NO_OID_SET)
		mechs = _gss_mech_oids;

	set.count = 1;
	min_time = GSS_C_INDEFINITE;
	for (i = 0; i < mechs->count; i++) {
		m = _gss_find_mech_switch(&mechs->elements[i]);
		if (!m)
			continue;

		if (desired_name != GSS_C_NO_NAME) {
			mn = _gss_find_mn(name, &mechs->elements[i]);
			if (!mn)
				continue;
		}

		mc = malloc(sizeof(struct _gss_mechanism_cred));
		if (!mc) {
			continue;
		}
		mc->gmc_mech = m;
		mc->gmc_mech_oid = &m->gm_mech_oid;

		/*
		 * XXX Probably need to do something with actual_mechs.
		 */
		set.elements = &mechs->elements[i];
		major_status = m->gm_acquire_cred(minor_status,
		    (desired_name != GSS_C_NO_NAME
			? mn->gmn_name : GSS_C_NO_NAME),
		    time_req, &set, cred_usage,
		    &mc->gmc_cred, NULL, &time);
		if (major_status) {
			free(mc);
			continue;
		}
		if (time < min_time)
			min_time = time;

		if (actual_mechs) {
			major_status = gss_add_oid_set_member(minor_status,
			    mc->gmc_mech_oid, actual_mechs);
			if (major_status) {
				m->gm_release_cred(minor_status,
				    &mc->gmc_cred);
				free(mc);
				continue;
			}
		}

		SLIST_INSERT_HEAD(&cred->gc_mc, mc, gmc_link);
	}

	/*
	 * If we didn't manage to create a single credential, return
	 * an error.
	 */
	if (!SLIST_FIRST(&cred->gc_mc)) {
		free(cred);
		if (actual_mechs)
			gss_release_oid_set(minor_status, actual_mechs);
		*output_cred_handle = 0;
		*minor_status = 0;
		return (GSS_S_NO_CRED);
	}

	if (time_rec)
		*time_rec = min_time;
	*output_cred_handle = (gss_cred_id_t) cred;
	*minor_status = 0;
	return (GSS_S_COMPLETE);
}
