/*-------------------------------------------------------------------------
 *
 * plsample.c
 *	  Handler for the PL/Sample procedural language
 *
 * Portions Copyright (c) 1996-2021, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 * IDENTIFICATION
 *		src/test/modules/plsample/plsample.c
 *
 *-------------------------------------------------------------------------
 */

#include "postgres.h"

#include "catalog/pg_proc.h"
#include "catalog/pg_type.h"
#include "commands/event_trigger.h"
#include "commands/trigger.h"
#include "executor/spi.h"
#include "funcapi.h"
#include "utils/builtins.h"
#include "utils/lsyscache.h"
#include "utils/syscache.h"

PG_MODULE_MAGIC;

PG_FUNCTION_INFO_V1(plsample_call_handler);

static Datum plsample_func_handler(PG_FUNCTION_ARGS);
static HeapTuple plsample_trigger_handler(PG_FUNCTION_ARGS);

/*
 * Handle function, procedure, and trigger calls.
 */
Datum
plsample_call_handler(PG_FUNCTION_ARGS)
{
	Datum		retval = (Datum) 0;

	PG_TRY();
	{
		/*
		 * Determine if called as function or trigger and call appropriate
		 * subhandler.
		 */
		if (CALLED_AS_TRIGGER(fcinfo))
		{
			/*
			 * This function has been called as a trigger function, where
			 * (TriggerData *) fcinfo->context includes the information of the
			 * context.
			 */
			retval = PointerGetDatum(plsample_trigger_handler(fcinfo));
			ereport(NOTICE, (errmsg("out")));
		}
		else if (CALLED_AS_EVENT_TRIGGER(fcinfo))
		{
			/*
			 * This function is called as an event trigger function, where
			 * (EventTriggerData *) fcinfo->context includes the information
			 * of the context.
			 */
		}
		else
		{
			/* Regular function handler */
			retval = plsample_func_handler(fcinfo);
		}
	}
	PG_FINALLY();
	{
	}
	PG_END_TRY();

	return retval;
}

/*
 * plsample_func_handler
 *
 * Function called by the call handler for function execution.
 */
static Datum
plsample_func_handler(PG_FUNCTION_ARGS)
{
	HeapTuple	pl_tuple;
	Datum		ret;
	char	   *source;
	bool		isnull;
	FmgrInfo   *arg_out_func;
	Form_pg_type type_struct;
	HeapTuple	type_tuple;
	Form_pg_proc pl_struct;
	volatile MemoryContext proc_cxt = NULL;
	Oid		   *argtypes;
	char	  **argnames;
	char	   *argmodes;
	char	   *proname;
	Form_pg_type pg_type_entry;
	Oid			result_typioparam;
	Oid			prorettype;
	FmgrInfo	result_in_func;
	int			numargs;

	/* Fetch the source text of the function. */
	pl_tuple = SearchSysCache(PROCOID,
							  ObjectIdGetDatum(fcinfo->flinfo->fn_oid), 0, 0, 0);
	if (!HeapTupleIsValid(pl_tuple))
		elog(ERROR, "cache lookup failed for function %u",
			 fcinfo->flinfo->fn_oid);

	/*
	 * Extract and print the source text of the function.  This can be used as
	 * a base for the function validation and execution.
	 */
	pl_struct = (Form_pg_proc) GETSTRUCT(pl_tuple);
	proname = pstrdup(NameStr(pl_struct->proname));
	ret = SysCacheGetAttr(PROCOID, pl_tuple, Anum_pg_proc_prosrc, &isnull);
	if (isnull)
		elog(ERROR, "could not find source text of function \"%s\"",
			 proname);
	source = DatumGetCString(DirectFunctionCall1(textout, ret));
	ereport(NOTICE,
			(errmsg("source text of function \"%s\": %s",
					proname, source)));

	/*
	 * Allocate a context that will hold all the Postgres data for the
	 * procedure.
	 */
	proc_cxt = AllocSetContextCreate(TopMemoryContext,
									 "PL/Sample function",
									 ALLOCSET_SMALL_SIZES);

	arg_out_func = (FmgrInfo *) palloc0(fcinfo->nargs * sizeof(FmgrInfo));
	numargs = get_func_arg_info(pl_tuple, &argtypes, &argnames, &argmodes);

	/*
	 * Iterate through all of the function arguments, printing each input
	 * value.
	 */
	for (int i = 0; i < numargs; i++)
	{
		Oid			argtype = pl_struct->proargtypes.values[i];
		char	   *value;

		type_tuple = SearchSysCache1(TYPEOID, ObjectIdGetDatum(argtype));
		if (!HeapTupleIsValid(type_tuple))
			elog(ERROR, "cache lookup failed for type %u", argtype);

		type_struct = (Form_pg_type) GETSTRUCT(type_tuple);
		fmgr_info_cxt(type_struct->typoutput, &(arg_out_func[i]), proc_cxt);
		ReleaseSysCache(type_tuple);

		value = OutputFunctionCall(&arg_out_func[i], fcinfo->args[i].value);
		ereport(NOTICE,
				(errmsg("argument: %d; name: %s; value: %s",
						i, argnames[i], value)));
	}

	/* Type of the result */
	prorettype = pl_struct->prorettype;
	ReleaseSysCache(pl_tuple);

	/*
	 * Get the required information for input conversion of the return value.
	 *
	 * If the function uses VOID as result, it is better to return NULL.
	 * Anyway, let's be honest.  This is just a template, so there is not much
	 * we can do here.  This returns NULL except if the result type is text,
	 * where the result is the source text of the function.
	 */
	if (prorettype != TEXTOID)
		PG_RETURN_NULL();

	type_tuple = SearchSysCache1(TYPEOID,
								 ObjectIdGetDatum(prorettype));
	if (!HeapTupleIsValid(type_tuple))
		elog(ERROR, "cache lookup failed for type %u", prorettype);
	pg_type_entry = (Form_pg_type) GETSTRUCT(type_tuple);
	result_typioparam = getTypeIOParam(type_tuple);

	fmgr_info_cxt(pg_type_entry->typinput, &result_in_func, proc_cxt);
	ReleaseSysCache(type_tuple);

	ret = InputFunctionCall(&result_in_func, source, result_typioparam, -1);
	PG_RETURN_DATUM(ret);
}

/*
 * plsample_trigger_handler
 *
 * Function called by the call handler for trigger execution.
 */
static HeapTuple
plsample_trigger_handler(PG_FUNCTION_ARGS)
{
	TriggerData *trigdata = (TriggerData *) fcinfo->context;
	char	   *string;
	volatile HeapTuple rettup;
	HeapTuple	pl_tuple;
	Datum		ret;
	char	   *source;
	bool		isnull;
	Form_pg_proc pl_struct;
	char	   *proname;
	int			rc PG_USED_FOR_ASSERTS_ONLY;
	MemoryContext proc_cxt = NULL;
	MemoryContext old_cxt = NULL;
	char	   *trigger_code = NULL;

	/* Make sure this is being called from a trigger. */
	if (!CALLED_AS_TRIGGER(fcinfo))
		elog(ERROR, "not called by trigger manager");

	/* Connect to the SPI manager */
	if (SPI_connect() != SPI_OK_CONNECT)
		elog(ERROR, "could not connect to SPI manager");

	rc = SPI_register_trigger_data(trigdata);
	Assert(rc >= 0);

	/* Fetch the source text of the function. */
	pl_tuple = SearchSysCache(PROCOID,
							  ObjectIdGetDatum(fcinfo->flinfo->fn_oid), 0, 0, 0);
	if (!HeapTupleIsValid(pl_tuple))
		elog(ERROR, "cache lookup failed for function %u",
			 fcinfo->flinfo->fn_oid);

	/*
	 * Extract and print the source text of the function.  This can be used as
	 * a base for the function validation and execution.
	 */
	pl_struct = (Form_pg_proc) GETSTRUCT(pl_tuple);
	proname = pstrdup(NameStr(pl_struct->proname));
	ret = SysCacheGetAttr(PROCOID, pl_tuple, Anum_pg_proc_prosrc, &isnull);
	if (isnull)
		elog(ERROR, "could not find source text of function \"%s\"",
			 proname);
	source = DatumGetCString(DirectFunctionCall1(textout, ret));
	ereport(NOTICE,
			(errmsg("source text of function \"%s\": %s",
					proname, source)));

	PG_TRY();
	{
		char	   *internal_args = "TD_name, TD_relid, TD_table_name, TD_table_schema, TD_event, TD_when, TD_level, TD_NEW, TD_OLD, args";

		/*
		 * The code generated for this trigger is: proname(internal_args)
		 * source
		 */
		int			length =
		strlen(proname) + strlen(source) + strlen(internal_args) + 4;

		/*
		 * Allocate a context that will hold all the Postgres data for the
		 * procedure.
		 */
		proc_cxt = AllocSetContextCreate(TopMemoryContext,
										 "PL/Sample function", ALLOCSET_SMALL_SIZES);

		trigger_code = (char *) palloc0(length * sizeof(char));
		trigger_code[0] = '\0';
		strcpy(trigger_code, proname);
		strcat(trigger_code, "(");
		strcat(trigger_code, internal_args);
		strcat(trigger_code, ") ");
		strcat(trigger_code, source);

		ereport(NOTICE,
				(errmsg("compile trigger function: %s", trigger_code)));

		old_cxt = MemoryContextSwitchTo(proc_cxt);

		MemoryContextSwitchTo(old_cxt);
	}
	PG_CATCH();
	{
		PG_RE_THROW();
	}
	PG_END_TRY();

	pfree(source);

	ReleaseSysCache(pl_tuple);

	PG_TRY();
	{
		int			i;

		ereport(NOTICE,
				(errmsg("trigger name: %s", trigdata->tg_trigger->tgname)));
		string = SPI_getrelname(trigdata->tg_relation);
		ereport(NOTICE, (errmsg("trigger relation: %s", string)));
		pfree(string);

		string = SPI_getnspname(trigdata->tg_relation);
		ereport(NOTICE, (errmsg("trigger relation schema: %s", string)));
		pfree(string);

		/* Example handling of different trigger aspects. */

		if (TRIGGER_FIRED_BY_INSERT(trigdata->tg_event))
		{
			ereport(NOTICE, (errmsg("triggered by INSERT")));
			rettup = trigdata->tg_trigtuple;
		}
		else if (TRIGGER_FIRED_BY_DELETE(trigdata->tg_event))
		{
			ereport(NOTICE, (errmsg("triggered by DELETE")));
			rettup = trigdata->tg_trigtuple;
		}
		else if (TRIGGER_FIRED_BY_UPDATE(trigdata->tg_event))
		{
			ereport(NOTICE, (errmsg("triggered by UPDATE")));
			rettup = trigdata->tg_trigtuple;
		}
		else if (TRIGGER_FIRED_BY_TRUNCATE(trigdata->tg_event))
		{
			ereport(NOTICE, (errmsg("triggered by TRUNCATE")));
			rettup = trigdata->tg_trigtuple;
		}
		else
			elog(ERROR, "unrecognized event: %u", trigdata->tg_event);

		if (TRIGGER_FIRED_BEFORE(trigdata->tg_event))
			ereport(NOTICE, (errmsg("triggered BEFORE")));
		else if (TRIGGER_FIRED_AFTER(trigdata->tg_event))
			ereport(NOTICE, (errmsg("triggered AFTER")));
		else if (TRIGGER_FIRED_INSTEAD(trigdata->tg_event))
			ereport(NOTICE, (errmsg("triggered INSTEAD OF")));
		else
			elog(ERROR, "unrecognized when: %u", trigdata->tg_event);

		if (TRIGGER_FIRED_FOR_ROW(trigdata->tg_event))
			ereport(NOTICE, (errmsg("triggered per row")));
		else if (TRIGGER_FIRED_FOR_STATEMENT(trigdata->tg_event))
			ereport(NOTICE, (errmsg("triggered per statement")));
		else
			elog(ERROR, "unrecognized level: %u", trigdata->tg_event);

		/*
		 * Iterate through all of the trigger arguments, printing each input
		 * value.
		 */
		for (i = 0; i < trigdata->tg_trigger->tgnargs; i++)
			ereport(NOTICE,
					(errmsg("trigger arg[%i]: %s", i,
							trigdata->tg_trigger->tgargs[i])));
	}
	PG_CATCH();
	{
		PG_RE_THROW();
	}
	PG_END_TRY();

	if (SPI_finish() != SPI_OK_FINISH)
		elog(ERROR, "SPI_finish() failed");

	return rettup;
}
