/******************************************************************************
 *
 * Module Name: psargs - Parse AML opcode arguments
 *              $Revision: 64 $
 *
 *****************************************************************************/

/*
 *  Copyright (C) 2000 - 2002, R. Byron Moore
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */


#include "acpi.h"
#include "acparser.h"
#include "amlcode.h"
#include "acnamesp.h"

#define _COMPONENT          ACPI_PARSER
	 ACPI_MODULE_NAME    ("psargs")


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ps_get_next_package_length
 *
 * PARAMETERS:  Parser_state        - Current parser state object
 *
 * RETURN:      Decoded package length.  On completion, the AML pointer points
 *              past the length byte or bytes.
 *
 * DESCRIPTION: Decode and return a package length field
 *
 ******************************************************************************/

u32
acpi_ps_get_next_package_length (
	acpi_parse_state        *parser_state)
{
	u32                     encoded_length;
	u32                     length = 0;


	ACPI_FUNCTION_TRACE ("Ps_get_next_package_length");


	encoded_length = (u32) ACPI_GET8 (parser_state->aml);
	parser_state->aml++;


	switch (encoded_length >> 6) /* bits 6-7 contain encoding scheme */ {
	case 0: /* 1-byte encoding (bits 0-5) */

		length = (encoded_length & 0x3F);
		break;


	case 1: /* 2-byte encoding (next byte + bits 0-3) */

		length = ((ACPI_GET8 (parser_state->aml) << 04) |
				 (encoded_length & 0x0F));
		parser_state->aml++;
		break;


	case 2: /* 3-byte encoding (next 2 bytes + bits 0-3) */

		length = ((ACPI_GET8 (parser_state->aml + 1) << 12) |
				  (ACPI_GET8 (parser_state->aml)    << 04) |
				  (encoded_length & 0x0F));
		parser_state->aml += 2;
		break;


	case 3: /* 4-byte encoding (next 3 bytes + bits 0-3) */

		length = ((ACPI_GET8 (parser_state->aml + 2) << 20) |
				  (ACPI_GET8 (parser_state->aml + 1) << 12) |
				  (ACPI_GET8 (parser_state->aml)    << 04) |
				  (encoded_length & 0x0F));
		parser_state->aml += 3;
		break;

	default:
		/* Can't get here, only 2 bits / 4 cases */
		break;
	}

	return_VALUE (length);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ps_get_next_package_end
 *
 * PARAMETERS:  Parser_state        - Current parser state object
 *
 * RETURN:      Pointer to end-of-package +1
 *
 * DESCRIPTION: Get next package length and return a pointer past the end of
 *              the package.  Consumes the package length field
 *
 ******************************************************************************/

u8 *
acpi_ps_get_next_package_end (
	acpi_parse_state        *parser_state)
{
	u8                      *start = parser_state->aml;
	NATIVE_UINT             length;


	ACPI_FUNCTION_TRACE ("Ps_get_next_package_end");


	length = (NATIVE_UINT) acpi_ps_get_next_package_length (parser_state);

	return_PTR (start + length); /* end of package */
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ps_get_next_namestring
 *
 * PARAMETERS:  Parser_state        - Current parser state object
 *
 * RETURN:      Pointer to the start of the name string (pointer points into
 *              the AML.
 *
 * DESCRIPTION: Get next raw namestring within the AML stream.  Handles all name
 *              prefix characters.  Set parser state to point past the string.
 *              (Name is consumed from the AML.)
 *
 ******************************************************************************/

NATIVE_CHAR *
acpi_ps_get_next_namestring (
	acpi_parse_state        *parser_state)
{
	u8                      *start = parser_state->aml;
	u8                      *end = parser_state->aml;


	ACPI_FUNCTION_TRACE ("Ps_get_next_namestring");


	/* Handle multiple prefix characters */

	while (acpi_ps_is_prefix_char (ACPI_GET8 (end))) {
		/* Include prefix '\\' or '^' */

		end++;
	}

	/* Decode the path */

	switch (ACPI_GET8 (end)) {
	case 0:

		/* Null_name */

		if (end == start) {
			start = NULL;
		}
		end++;
		break;

	case AML_DUAL_NAME_PREFIX:

		/* Two name segments */

		end += 9;
		break;

	case AML_MULTI_NAME_PREFIX_OP:

		/* Multiple name segments, 4 chars each */

		end += 2 + ((ACPI_SIZE) ACPI_GET8 (end + 1) * 4);
		break;

	default:

		/* Single name segment */

		end += 4;
		break;
	}

	parser_state->aml = (u8*) end;
	return_PTR ((NATIVE_CHAR *) start);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ps_get_next_namepath
 *
 * PARAMETERS:  Parser_state        - Current parser state object
 *              Arg                 - Where the namepath will be stored
 *              Arg_count           - If the namepath points to a control method
 *                                    the method's argument is returned here.
 *              Method_call         - Whether the namepath can be the start
 *                                    of a method call
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Get next name (if method call, return # of required args).
 *              Names are looked up in the internal namespace to determine
 *              if the name represents a control method.  If a method
 *              is found, the number of arguments to the method is returned.
 *              This information is critical for parsing to continue correctly.
 *
 ******************************************************************************/

acpi_status
acpi_ps_get_next_namepath (
	acpi_parse_state        *parser_state,
	acpi_parse_object       *arg,
	u32                     *arg_count,
	u8                      method_call)
{
	NATIVE_CHAR             *path;
	acpi_parse_object       *name_op;
	acpi_status             status = AE_OK;
	acpi_operand_object     *method_desc;
	acpi_namespace_node     *node;
	acpi_generic_state      scope_info;


	ACPI_FUNCTION_TRACE ("Ps_get_next_namepath");


	path = acpi_ps_get_next_namestring (parser_state);

	/* Null path case is allowed */

	if (path) {
		/*
		 * Lookup the name in the internal namespace
		 */
		scope_info.scope.node = NULL;
		node = parser_state->start_node;
		if (node) {
			scope_info.scope.node = node;
		}

		/*
		 * Lookup object.  We don't want to add anything new to the namespace
		 * here, however.  So we use MODE_EXECUTE.  Allow searching of the
		 * parent tree, but don't open a new scope -- we just want to lookup the
		 * object  (MUST BE mode EXECUTE to perform upsearch)
		 */
		status = acpi_ns_lookup (&scope_info, path, ACPI_TYPE_ANY, ACPI_IMODE_EXECUTE,
				 ACPI_NS_SEARCH_PARENT | ACPI_NS_DONT_OPEN_SCOPE, NULL,
				 &node);
		if (ACPI_SUCCESS (status) && method_call) {
			if (node->type == ACPI_TYPE_METHOD) {
				method_desc = acpi_ns_get_attached_object (node);
				ACPI_DEBUG_PRINT ((ACPI_DB_PARSE, "Control Method - %p Desc %p Path=%p\n",
					node, method_desc, path));

				name_op = acpi_ps_alloc_op (AML_INT_NAMEPATH_OP);
				if (!name_op) {
					return_ACPI_STATUS (AE_NO_MEMORY);
				}

				/* Change arg into a METHOD CALL and attach name to it */

				acpi_ps_init_op (arg, AML_INT_METHODCALL_OP);

				name_op->common.value.name = path;

				/* Point METHODCALL/NAME to the METHOD Node */

				name_op->common.node = node;
				acpi_ps_append_arg (arg, name_op);

				if (!method_desc) {
					ACPI_DEBUG_PRINT ((ACPI_DB_PARSE, "Control Method - %p has no attached object\n",
						node));
					return_ACPI_STATUS (AE_AML_INTERNAL);
				}

				ACPI_DEBUG_PRINT ((ACPI_DB_PARSE, "Control Method - %p Args %X\n",
					node, method_desc->method.param_count));

				*arg_count = method_desc->method.param_count;
				return_ACPI_STATUS (AE_OK);
			}

			/*
			 * Else this is normal named object reference.
			 * Just init the NAMEPATH object with the pathname.
			 * (See code below)
			 */
		}
	}

	/*
	 * Regardless of success/failure above,
	 * Just initialize the Op with the pathname.
	 */
	acpi_ps_init_op (arg, AML_INT_NAMEPATH_OP);
	arg->common.value.name = path;

	return_ACPI_STATUS (status);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ps_get_next_simple_arg
 *
 * PARAMETERS:  Parser_state        - Current parser state object
 *              Arg_type            - The argument type (AML_*_ARG)
 *              Arg                 - Where the argument is returned
 *
 * RETURN:      None
 *
 * DESCRIPTION: Get the next simple argument (constant, string, or namestring)
 *
 ******************************************************************************/

void
acpi_ps_get_next_simple_arg (
	acpi_parse_state        *parser_state,
	u32                     arg_type,
	acpi_parse_object       *arg)
{

	ACPI_FUNCTION_TRACE_U32 ("Ps_get_next_simple_arg", arg_type);


	switch (arg_type) {
	case ARGP_BYTEDATA:

		acpi_ps_init_op (arg, AML_BYTE_OP);
		arg->common.value.integer = (u32) ACPI_GET8 (parser_state->aml);
		parser_state->aml++;
		break;


	case ARGP_WORDDATA:

		acpi_ps_init_op (arg, AML_WORD_OP);

		/* Get 2 bytes from the AML stream */

		ACPI_MOVE_UNALIGNED16_TO_32 (&arg->common.value.integer, parser_state->aml);
		parser_state->aml += 2;
		break;


	case ARGP_DWORDDATA:

		acpi_ps_init_op (arg, AML_DWORD_OP);

		/* Get 4 bytes from the AML stream */

		ACPI_MOVE_UNALIGNED32_TO_32 (&arg->common.value.integer, parser_state->aml);
		parser_state->aml += 4;
		break;


	case ARGP_QWORDDATA:

		acpi_ps_init_op (arg, AML_QWORD_OP);

		/* Get 8 bytes from the AML stream */

		ACPI_MOVE_UNALIGNED64_TO_64 (&arg->common.value.integer, parser_state->aml);
		parser_state->aml += 8;
		break;


	case ARGP_CHARLIST:

		acpi_ps_init_op (arg, AML_STRING_OP);
		arg->common.value.string = (char *) parser_state->aml;

		while (ACPI_GET8 (parser_state->aml) != '\0') {
			parser_state->aml++;
		}
		parser_state->aml++;
		break;


	case ARGP_NAME:
	case ARGP_NAMESTRING:

		acpi_ps_init_op (arg, AML_INT_NAMEPATH_OP);
		arg->common.value.name = acpi_ps_get_next_namestring (parser_state);
		break;


	default:
		ACPI_REPORT_ERROR (("Invalid Arg_type %X\n", arg_type));
		break;
	}

	return_VOID;
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ps_get_next_field
 *
 * PARAMETERS:  Parser_state        - Current parser state object
 *
 * RETURN:      A newly allocated FIELD op
 *
 * DESCRIPTION: Get next field (Named_field, Reserved_field, or Access_field)
 *
 ******************************************************************************/

acpi_parse_object *
acpi_ps_get_next_field (
	acpi_parse_state        *parser_state)
{
	u32                     aml_offset = ACPI_PTR_DIFF (parser_state->aml,
			  parser_state->aml_start);
	acpi_parse_object       *field;
	u16                     opcode;
	u32                     name;


	ACPI_FUNCTION_TRACE ("Ps_get_next_field");


	/* determine field type */

	switch (ACPI_GET8 (parser_state->aml)) {
	default:

		opcode = AML_INT_NAMEDFIELD_OP;
		break;

	case 0x00:

		opcode = AML_INT_RESERVEDFIELD_OP;
		parser_state->aml++;
		break;

	case 0x01:

		opcode = AML_INT_ACCESSFIELD_OP;
		parser_state->aml++;
		break;
	}


	/* Allocate a new field op */

	field = acpi_ps_alloc_op (opcode);
	if (!field) {
		return_PTR (NULL);
	}

	field->common.aml_offset = aml_offset;

	/* Decode the field type */

	switch (opcode) {
	case AML_INT_NAMEDFIELD_OP:

		/* Get the 4-character name */

		ACPI_MOVE_UNALIGNED32_TO_32 (&name, parser_state->aml);
		acpi_ps_set_name (field, name);
		parser_state->aml += 4;

		/* Get the length which is encoded as a package length */

		field->common.value.size = acpi_ps_get_next_package_length (parser_state);
		break;


	case AML_INT_RESERVEDFIELD_OP:

		/* Get the length which is encoded as a package length */

		field->common.value.size = acpi_ps_get_next_package_length (parser_state);
		break;


	case AML_INT_ACCESSFIELD_OP:

		/*
		 * Get Access_type and Access_attrib and merge into the field Op
		 * Access_type is first operand, Access_attribute is second
		 */
		field->common.value.integer32 = (ACPI_GET8 (parser_state->aml) << 8);
		parser_state->aml++;
		field->common.value.integer32 |= ACPI_GET8 (parser_state->aml);
		parser_state->aml++;
		break;

	default:
		/* Opcode was set in previous switch */
		break;
	}

	return_PTR (field);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ps_get_next_arg
 *
 * PARAMETERS:  Parser_state        - Current parser state object
 *              Arg_type            - The argument type (AML_*_ARG)
 *              Arg_count           - If the argument points to a control method
 *                                    the method's argument is returned here.
 *
 * RETURN:      Status, and an op object containing the next argument.
 *
 * DESCRIPTION: Get next argument (including complex list arguments that require
 *              pushing the parser stack)
 *
 ******************************************************************************/

acpi_status
acpi_ps_get_next_arg (
	acpi_parse_state        *parser_state,
	u32                     arg_type,
	u32                     *arg_count,
	acpi_parse_object       **return_arg)
{
	acpi_parse_object       *arg = NULL;
	acpi_parse_object       *prev = NULL;
	acpi_parse_object       *field;
	u32                     subop;
	acpi_status             status = AE_OK;


	ACPI_FUNCTION_TRACE_PTR ("Ps_get_next_arg", parser_state);


	switch (arg_type) {
	case ARGP_BYTEDATA:
	case ARGP_WORDDATA:
	case ARGP_DWORDDATA:
	case ARGP_CHARLIST:
	case ARGP_NAME:
	case ARGP_NAMESTRING:

		/* constants, strings, and namestrings are all the same size */

		arg = acpi_ps_alloc_op (AML_BYTE_OP);
		if (!arg) {
			return_ACPI_STATUS (AE_NO_MEMORY);
		}
		acpi_ps_get_next_simple_arg (parser_state, arg_type, arg);
		break;


	case ARGP_PKGLENGTH:

		/* Package length, nothing returned */

		parser_state->pkg_end = acpi_ps_get_next_package_end (parser_state);
		break;


	case ARGP_FIELDLIST:

		if (parser_state->aml < parser_state->pkg_end) {
			/* Non-empty list */

			while (parser_state->aml < parser_state->pkg_end) {
				field = acpi_ps_get_next_field (parser_state);
				if (!field) {
					return_ACPI_STATUS (AE_NO_MEMORY);
				}

				if (prev) {
					prev->common.next = field;
				}
				else {
					arg = field;
				}

				prev = field;
			}

			/* Skip to End of byte data */

			parser_state->aml = parser_state->pkg_end;
		}
		break;


	case ARGP_BYTELIST:

		if (parser_state->aml < parser_state->pkg_end) {
			/* Non-empty list */

			arg = acpi_ps_alloc_op (AML_INT_BYTELIST_OP);
			if (!arg) {
				return_ACPI_STATUS (AE_NO_MEMORY);
			}

			/* Fill in bytelist data */

			arg->common.value.size = ACPI_PTR_DIFF (parser_state->pkg_end, parser_state->aml);
			arg->named.data = parser_state->aml;

			/* Skip to End of byte data */

			parser_state->aml = parser_state->pkg_end;
		}
		break;


	case ARGP_TARGET:
	case ARGP_SUPERNAME:
	case ARGP_SIMPLENAME:

		subop = acpi_ps_peek_opcode (parser_state);
		if (subop == 0                  ||
			acpi_ps_is_leading_char (subop) ||
			acpi_ps_is_prefix_char (subop)) {
			/* Null_name or Name_string */

			arg = acpi_ps_alloc_op (AML_INT_NAMEPATH_OP);
			if (!arg) {
				return_ACPI_STATUS (AE_NO_MEMORY);
			}

			status = acpi_ps_get_next_namepath (parser_state, arg, arg_count, 0);
		}
		else {
			/* single complex argument, nothing returned */

			*arg_count = 1;
		}
		break;


	case ARGP_DATAOBJ:
	case ARGP_TERMARG:

		/* single complex argument, nothing returned */

		*arg_count = 1;
		break;


	case ARGP_DATAOBJLIST:
	case ARGP_TERMLIST:
	case ARGP_OBJLIST:

		if (parser_state->aml < parser_state->pkg_end) {
			/* non-empty list of variable arguments, nothing returned */

			*arg_count = ACPI_VAR_ARGS;
		}
		break;


	default:

		ACPI_REPORT_ERROR (("Invalid Arg_type: %X\n", arg_type));
		status = AE_AML_OPERAND_TYPE;
		break;
	}

	*return_arg = arg;
	return_ACPI_STATUS (status);
}
