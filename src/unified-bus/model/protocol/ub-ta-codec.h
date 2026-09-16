// SPDX-License-Identifier: GPL-2.0-only
#ifndef UB_TA_CODEC_H
#define UB_TA_CODEC_H

#include "ns3/ub-datatype.h"
#include "ns3/ub-header.h"
#include "ns3/ub-transaction-rules.h"

#include <array>

namespace ns3
{

/**
 * Wire presence of the transaction headers that a basic transaction header flags.
 *
 * TV_EN selects TVETAH, MT_EN selects MTETAH, and UD_FLG selects UDETAH. Headers without such a
 * flag are planned by the rule layout alone and are never guessed from the remaining bytes.
 */
struct UbTaHeaderPresence
{
    /** TV_EN / TK_VLD: the unit carries a TVETAH. */
    bool tokenValue{false};
    /** MT_EN: the unit carries a MTETAH. */
    bool messageTarget{false};
    /** UD_FLG: the unit carries a UDETAH. */
    bool userData{false};
};

/**
 * Ordered transaction-header plan of one TA unit, derived from its static transaction rule.
 *
 * The plan is the single source of wire header order and presence: a carrier serializes exactly the
 * planned headers and a decoder consumes exactly the planned headers, so the two carriers and the
 * two directions cannot drift apart. The basic header (BTAH or ATAH) is not part of the plan
 * because its format is carrier specific; it is always written last by a builder and read first by
 * a decoder.
 */
class UbTaHeaderPlan
{
  public:
    /**
     * Plan the extension headers of one request.
     *
     * Flag-driven headers are planned only when their flag is set; every other header listed by the
     * rule is operation-required and is always planned.
     */
    static UbTaHeaderPlan ForRequest(const UbTransactionRule& rule, UbTaHeaderPresence presence);
    /**
     * Plan the extension headers of one response.
     *
     * A response carries ATAH, which has no header presence flags, so flag-driven headers are never
     * planned for a response.
     */
    static UbTaHeaderPlan ForResponse(const UbTransactionRule& rule);

    /** Number of planned extension headers, excluding the carrier basic header. */
    uint8_t GetCount() const { return m_count; }
    /** Header kind at one wire position in wire order; index 0 follows the basic header. */
    UbTransactionHeaderKind GetKind(uint8_t wireIndex) const { return m_kinds[wireIndex]; }
    /** Whether one header kind is planned for this unit. */
    bool Contains(UbTransactionHeaderKind kind) const;
    /** Presence this plan was built with, so a builder can declare the same basic-header flags. */
    const UbTaHeaderPresence& GetPresence() const { return m_presence; }
    /**
     * Serialized size of the planned extension headers, excluding the basic header.
     *
     * @param maetahBytes MAETAH size of the carrier format that will write this plan.
     */
    uint32_t GetExtensionBytes(uint32_t maetahBytes) const;

  private:
    static UbTaHeaderPlan Build(const UbTransactionHeaderKind* kinds,
                                uint8_t count,
                                UbTaHeaderPresence presence);

    std::array<UbTransactionHeaderKind, 8> m_kinds{};
    uint8_t m_count{0};
    UbTaHeaderPresence m_presence{};
};

/** Serialized MAETAH size in bytes of the full (RTP) transaction header format. */
constexpr uint32_t UB_TA_FULL_MAETAH_BYTES = 16;
/** Serialized MAETAH size in bytes of the compact (CTP) transaction header format. */
constexpr uint32_t UB_TA_COMPACT_MAETAH_BYTES = 12;
/** Serialized BTAH/ATAH size in bytes of the full (RTP) transaction header format. */
constexpr uint32_t UB_TA_FULL_BASE_HEADER_BYTES = 8;
/** Serialized BTAH/ATAH size in bytes of the compact (CTP) transaction header format. */
constexpr uint32_t UB_TA_COMPACT_BASE_HEADER_BYTES = 4;

/**
 * Declare the header presence a builder must announce for one request.
 *
 * A flag-driven header is announced when the rule lists it and its normalized field value exists;
 * the phase-one field model has no user-data value, so UD_FLG stays off.
 */
UbTaHeaderPresence DeclareRequestHeaderPresence(const UbTransactionRule& rule,
                                                const UbTransactionFields& fields);

/**
 * The header presence a compact (CTP) basic transaction header can express.
 *
 * The compact basic header has no MT_EN bit, so MTETAH is carried whenever the rule lists it. Its
 * TK_VLD and UD_FLG bits are not settable or readable in this phase, so those headers are never
 * planned for the compact carrier.
 */
UbTaHeaderPresence CompactHeaderPresence(const UbTransactionRule& rule);

/**
 * Write the field values of one planned extension header.
 *
 * @return false when the header kind carries no normalized field value in this phase; the caller
 * must then leave the header out instead of inventing a default.
 */
bool WriteTransactionExtension(const UbTransactionFields& fields,
                               UbTransactionExtensionHeader& header);

/** Read one decoded extension header into the normalized field set. */
void ReadTransactionExtension(const UbTransactionExtensionHeader& header,
                              UbTransactionFields& fields);

} // namespace ns3

#endif
