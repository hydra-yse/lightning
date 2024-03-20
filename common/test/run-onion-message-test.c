/* Creates test vector bolt04/blinded-onion-message-onion-test.json.  Run output through jq! */
static void maybe_print(const char *fmt, ...);
#define SUPERVERBOSE maybe_print
#include "config.h"
#include "../../wire/fromwire.c"
#include "../../wire/tlvstream.c"
#include "../../wire/towire.c"
#include "../amount.c"
#include "../bigsize.c"
#include "../blindedpath.c"
#include "../blindedpay.c"
#include "../blinding.c"
#include "../features.c"
#include "../hmac.c"
#include "../onion_encode.c"
#include "../onion_message_parse.c"
#include "../sphinx.c"
#include "../../wire/onion_wiregen.c"
#include "../../wire/peer_wiregen.c"
#include <common/ecdh.h>
#include <common/setup.h>
#include <stdio.h>

/* AUTOGENERATED MOCKS START */
/* Generated stub for fromwire_channel_id */
bool fromwire_channel_id(const u8 **cursor UNNEEDED, size_t *max UNNEEDED,
			 struct channel_id *channel_id UNNEEDED)
{ fprintf(stderr, "fromwire_channel_id called!\n"); abort(); }
/* Generated stub for fromwire_node_id */
void fromwire_node_id(const u8 **cursor UNNEEDED, size_t *max UNNEEDED, struct node_id *id UNNEEDED)
{ fprintf(stderr, "fromwire_node_id called!\n"); abort(); }
/* Generated stub for new_onionreply */
struct onionreply *new_onionreply(const tal_t *ctx UNNEEDED, const u8 *contents TAKES UNNEEDED)
{ fprintf(stderr, "new_onionreply called!\n"); abort(); }
/* Generated stub for pubkey_from_node_id */
bool pubkey_from_node_id(struct pubkey *key UNNEEDED, const struct node_id *id UNNEEDED)
{ fprintf(stderr, "pubkey_from_node_id called!\n"); abort(); }
/* Generated stub for status_fmt */
void status_fmt(enum log_level level UNNEEDED,
		const struct node_id *peer UNNEEDED,
		const char *fmt UNNEEDED, ...)

{ fprintf(stderr, "status_fmt called!\n"); abort(); }
/* Generated stub for towire_channel_id */
void towire_channel_id(u8 **pptr UNNEEDED, const struct channel_id *channel_id UNNEEDED)
{ fprintf(stderr, "towire_channel_id called!\n"); abort(); }
/* Generated stub for towire_node_id */
void towire_node_id(u8 **pptr UNNEEDED, const struct node_id *id UNNEEDED)
{ fprintf(stderr, "towire_node_id called!\n"); abort(); }
/* AUTOGENERATED MOCKS END */

static bool comma;

static void flush_comma(void)
{
	if (comma)
		printf(",\n");
	comma = false;
}

static void maybe_comma(void)
{
	comma = true;
}

static void json_start(const char *name, const char what)
{
	flush_comma();
	if (name)
		printf("\"%s\":", name);
	printf("%c\n", what);
}

static void json_end(const char what)
{
	comma = false;
	printf("%c\n", what);
}

static void json_strfield(const char *name, const char *val)
{
	flush_comma();
	printf("\t\"%s\": \"%s\"", name, val);
	maybe_comma();
}

static void json_hexfield(const char *name, const u8 *val, size_t len)
{
	json_strfield(name, tal_hexstr(tmpctx, val, len));
}

static void json_hex_talfield(const char *name, const u8 *val)
{
	json_strfield(name, tal_hexstr(tmpctx, val, tal_bytelen(val)));
}

static void json_pubkey(const char *name, const struct pubkey *key)
{
	json_strfield(name, fmt_pubkey(tmpctx, key));
}

static bool enable_superverbose;
static void maybe_print(const char *fmt, ...)
{
	if (enable_superverbose) {
		va_list ap;
		va_start(ap, fmt);
		vfprintf(stdout, fmt, ap);
		va_end(ap);
	}
}

/* Updated each time, as we pretend to be Alice, Bob, Carol */
static const struct privkey *mykey;

void ecdh(const struct pubkey *point, struct secret *ss)
{
	if (secp256k1_ecdh(secp256k1_ctx, ss->data, &point->pubkey,
			   mykey->secret.data, NULL, NULL) != 1)
		abort();
}

/* This established by trial and error. */
#define LARGEST_DAVE_TLV_SIZE 42

/* Generic, ugly, function to calc encrypted_recipient_data,
   alias and next blinding, and print out JSON */
static u8 *add_hop(const char *name,
		   const char *comment,
		   const struct pubkey *me,
		   const struct pubkey *next,
		   const struct privkey *override_blinding,
		   const char *additional_field_hexstr,
		   const char *path_id_hexstr,
		   struct privkey *blinding, /* in and out */
		   struct pubkey *alias /* out */)
{
	struct tlv_encrypted_data_tlv *tlv = tlv_encrypted_data_tlv_new(tmpctx);
	u8 *enctlv, *encrypted_recipient_data;
	const u8 *additional;

	json_start(NULL, '{');
	json_strfield("alias", name);
	json_strfield("comment", comment);
	json_hexfield("blinding_secret", blinding->secret.data, sizeof(*blinding));

	/* We have to calc first, to make padding correct */
	tlv->next_node_id = tal_dup_or_null(tlv, struct pubkey, next);
	if (path_id_hexstr)
		tlv->path_id = tal_hexdata(tlv, path_id_hexstr,
					   strlen(path_id_hexstr));

	/* Normally we wouldn't know blinding privkey, and we'd just
	 * paste in the rest of the path as given, but here we're actually
	 * generating the lot. */
	if (override_blinding) {
		tlv->next_blinding_override = tal(tlv, struct pubkey);
		assert(pubkey_from_privkey(override_blinding, tlv->next_blinding_override));
	}

	/* This is assumed to be a valid TLV tuple, and greater than
	 * any previous, so we simply append. */
	if (additional_field_hexstr)
		additional = tal_hexdata(tlv, additional_field_hexstr,
					 strlen(additional_field_hexstr));
	else
		additional = NULL;

	enctlv = tal_arr(tmpctx, u8, 0);
	towire_tlv_encrypted_data_tlv(&enctlv, tlv);

	/* Now create padding (in Dave's path) */
	if (tal_bytelen(enctlv) + tal_bytelen(additional)
	    < LARGEST_DAVE_TLV_SIZE) {
		/* Add padding: T and L take 2 bytes, even before V */
		assert(tal_bytelen(enctlv) + tal_bytelen(additional) + 2
		       <= LARGEST_DAVE_TLV_SIZE);
		tlv->padding = tal_arrz(tlv, u8,
					LARGEST_DAVE_TLV_SIZE
					- tal_bytelen(enctlv)
					- tal_bytelen(additional) - 2);
	}
	enctlv = tal_arr(tmpctx, u8, 0);
	towire_tlv_encrypted_data_tlv(&enctlv, tlv);
	towire(&enctlv, additional, tal_bytelen(additional));
	if (!override_blinding)
		assert(tal_bytelen(enctlv) == LARGEST_DAVE_TLV_SIZE);

	json_start("tlvs", '{');
	if (tlv->padding)
		json_hex_talfield("padding", tlv->padding);
	if (tlv->next_node_id)
		json_pubkey("next_node_id", tlv->next_node_id);
	if (tlv->path_id)
		json_hex_talfield("path_id", tlv->path_id);
	if (tlv->next_blinding_override) {
		json_pubkey("next_blinding_override",
			    tlv->next_blinding_override);
		json_hexfield("blinding_override_secret",
			      override_blinding->secret.data,
			      sizeof(*override_blinding));
	}
	if (additional) {
		/* Deconstruct into type, len, value */
		size_t totlen = tal_bytelen(additional);
		u64 type = fromwire_bigsize(&additional, &totlen);
		u64 len = fromwire_bigsize(&additional, &totlen);
		assert(len == totlen);
		json_hexfield(tal_fmt(tmpctx, "unknown_tag_%"PRIu64, type),
			      additional, len);
	}
	json_end('}');

	maybe_comma();
	json_hex_talfield("encrypted_data_tlv", enctlv);
	flush_comma();
	enable_superverbose = true;
	encrypted_recipient_data = enctlv_from_encmsg_raw(tmpctx,
							  blinding,
							  me,
							  enctlv,
							  blinding,
							  alias);
	enable_superverbose = false;
	json_hex_talfield("encrypted_recipient_data",
			  encrypted_recipient_data);
	if (override_blinding)
		*blinding = *override_blinding;

	json_end('}');
	maybe_comma();
	return encrypted_recipient_data;
}

int main(int argc, char *argv[])
{
	const char *alias[] = {"Alice", "Bob", "Carol", "Dave"};
	struct privkey privkey[ARRAY_SIZE(alias)], blinding, override_blinding;
	struct pubkey id[ARRAY_SIZE(alias)], blinding_pub;
	struct secret session_key;
	u8 *erd[ARRAY_SIZE(alias)]; /* encrypted_recipient_data */
	u8 *onion_message_packet, *onion_message;
	struct pubkey blinded[ARRAY_SIZE(alias)];
	struct sphinx_path *sphinx_path;
	struct onionpacket *op;
	struct secret *path_secrets;

	common_setup(argv[0]);

	/* Alice is AAA... Bob is BBB... */
	for (size_t i = 0; i < ARRAY_SIZE(alias); i++) {
		memset(&privkey[i], alias[i][0], sizeof(privkey[i]));
		assert(pubkey_from_privkey(&privkey[i], &id[i]));
	}

	memset(&session_key, 3, sizeof(session_key));

	json_start(NULL, '{');
	json_strfield("comment", "Test vector creating an onionmessage, including joining an existing one");
	json_start("generate", '{');
        json_strfield("comment",  "This sections contains test data for Dave's blinded path Bob->Dave; sender has to prepend a hop to Alice to reach Bob");
	json_hexfield("session_key", session_key.data, sizeof(session_key));
	json_start("hops", '[');

	memset(&blinding, 99, sizeof(blinding));
	memset(&override_blinding, 1, sizeof(override_blinding));
	erd[0] = add_hop("Alice", "Alice->Bob: note next_blinding_override to match that give by Dave for Bob",
			 &id[0], &id[1],
			 &override_blinding, NULL, NULL,
			 &blinding, &blinded[0]);
	erd[1] = add_hop("Bob", "Bob->Carol",
			 &id[1], &id[2],
			 NULL, "fd023103123456", NULL,
			 &blinding, &blinded[1]);
	erd[2] = add_hop("Carol", "Carol->Dave",
			 &id[2], &id[3],
			 NULL, NULL, NULL,
			 &blinding, &blinded[2]);
	erd[3] = add_hop("Dave", "Dave is final node, hence path_id",
			 &id[3], NULL,
			 NULL, "fdffff0206c1",
			 "deadbeefbadc0ffeedeadbeefbadc0ffeedeadbeefbadc0ffeedeadbeefbadc0",
			 &blinding, &blinded[3]);

	json_end(']');
	json_end('}');
	maybe_comma();

	memset(&blinding, 99, sizeof(blinding));
	assert(pubkey_from_privkey(&blinding, &blinding_pub));
	json_start("route", '{');
	json_strfield("comment", "The resulting blinded route Alice to Dave.");
	json_pubkey("introduction_node_id", &id[0]);
	json_pubkey("blinding", &blinding_pub);

	json_start("hops", '[');
	for (size_t i = 0; i < ARRAY_SIZE(erd); i++) {
		json_start(NULL, '{');
		if (i != 0)
			json_pubkey("blinded_node_id", &blinded[i]);
		json_hex_talfield("encrypted_recipient_data", erd[i]);
		json_end('}');
		maybe_comma();
	}
	json_end(']');
	json_end('}');
	maybe_comma();

	json_start("onionmessage", '{');
	json_strfield("comment", "An onion message which sends a 'hello' to Dave");
	json_strfield("unknown_tag_1", "68656c6c6f");

	/* Create the onionmessage */
	sphinx_path = sphinx_path_new(tmpctx, NULL);
	for (size_t i = 0; i < ARRAY_SIZE(erd); i++) {
		struct tlv_onionmsg_tlv *tlv = tlv_onionmsg_tlv_new(tmpctx);
		u8 *onionmsg_tlv;
		tlv->encrypted_recipient_data = erd[i];
		onionmsg_tlv = tal_arr(tmpctx, u8, 0);
		/* For final hop, add unknown 'hello' field */
		if (i == ARRAY_SIZE(erd) - 1) {
			towire_bigsize(&onionmsg_tlv, 1); /* type */
			towire_bigsize(&onionmsg_tlv, strlen("hello")); /* length */
			towire(&onionmsg_tlv, "hello", strlen("hello"));
		}
		towire_tlv_onionmsg_tlv(&onionmsg_tlv, tlv);
		sphinx_add_hop(sphinx_path, &blinded[i], onionmsg_tlv);
	}

	/* Make it use our session key! */
	sphinx_path->session_key = &session_key;

	/* BOLT-onion-message #4:
	 * - SHOULD set `onion_message_packet` `len` to 1366 or 32834.
	 */
	op = create_onionpacket(tmpctx, sphinx_path, ROUTING_INFO_SIZE,
				&path_secrets);
	onion_message_packet = serialize_onionpacket(tmpctx, op);
	json_hex_talfield("onion_message_packet", onion_message_packet);
	json_end('}');
	maybe_comma();

	json_start("decrypt", '{');
	json_strfield("comment", "This section contains the internal values generated by intermediate nodes when decrypting the onion.");

	onion_message = towire_onion_message(tmpctx, &blinding_pub, onion_message_packet);

	json_start("hops", '[');
	for (size_t i = 0; i < ARRAY_SIZE(erd); i++) {
		struct pubkey next_node_id;
		struct tlv_onionmsg_tlv *final_om;
		struct pubkey final_alias;
		struct secret *final_path_id;

		json_start(NULL, '{');
		json_strfield("alias", alias[i]);
		json_hexfield("privkey", privkey[i].secret.data, sizeof(privkey[i]));
		json_hex_talfield("onion_message", onion_message);

		/* Now, do full decrypt code, to check */
		assert(fromwire_onion_message(tmpctx, onion_message,
					      &blinding_pub, &onion_message_packet));

		/* For test_ecdh */
		mykey = &privkey[i];
		assert(onion_message_parse(tmpctx, onion_message_packet, &blinding_pub, NULL,
					   &id[i],
					   &onion_message, &next_node_id,
					   &final_om,
					   &final_alias,
					   &final_path_id));
		if (onion_message) {
			json_pubkey("next_node_id", &next_node_id);
		} else {
			const struct tlv_field *hello;
			json_start("tlvs", '{');
			json_hexfield("path_id", final_path_id->data, sizeof(*final_path_id));
			hello = &final_om->fields[0];
			json_hexfield(tal_fmt(tmpctx,
					      "unknown_tag_%"PRIu64,
					      hello->numtype),
				      hello->value,
				      hello->length);
			json_end('}');
		}
		json_end('}');
		maybe_comma();
	}
	json_end(']');
	json_end('}');
	json_end('}');
	common_shutdown();
}
