/* SPDX-FileCopyrightText: 2026 Dirk Wahrheit
 * SPDX-License-Identifier: Apache-2.0
 *
 * The vendored NMEA2000 library on a simulated bus, under a clock the test
 * drives. espos_n2k::Node leaves the protocol to the library, so these pin
 * down the behaviour the node relies on -- the address-claim contest and an
 * assembled fast-packet answer -- and fail if an update of
 * third_party/NMEA2000 changes it.
 */
#include <cstring>
#include <deque>
#include <vector>

#include "N2kMessages.h"
#include "NMEA2000.h"
#include "unity.h"

namespace
{

int64_t g_now_us = 1000000;

struct Frame {
    unsigned long id;
    unsigned char len;
    unsigned char data[8];
};

/* Every node's frames reach every other node, as on a wire. */
class SimNode;
std::vector<SimNode *> g_bus;

class SimNode : public tNMEA2000
{
  public:
    std::deque<Frame> inbox;
    std::vector<unsigned long> seen;

    SimNode()
    {
        g_bus.push_back(this);
    }

  protected:
    bool CANOpen() override
    {
        return true;
    }
    bool CANSendFrame(unsigned long id, unsigned char len, const unsigned char *buf, bool) override
    {
        Frame f { id, len, {} };
        memcpy(f.data, buf, len);
        for (SimNode *n : g_bus) {
            if (n != this) n->inbox.push_back(f);
        }
        return true;
    }
    bool CANGetFrame(unsigned long &id, unsigned char &len, unsigned char *buf) override
    {
        if (inbox.empty()) return false;
        const Frame &f = inbox.front();
        id = f.id;
        len = f.len;
        memcpy(buf, f.data, f.len);
        inbox.pop_front();
        return true;
    }
};

std::vector<unsigned long> *g_seen_by = nullptr;
void record(const tN2kMsg &m)
{
    if (g_seen_by) g_seen_by->push_back(m.PGN);
}

void run(int ms, SimNode &a, SimNode &b)
{
    for (int t = 0; t < ms; t++) {
        g_now_us += 1000;
        a.ParseMessages();
        b.ParseMessages();
    }
}

void setup(SimNode &n, unsigned long unique, uint8_t address)
{
    n.SetProductInformation("SN", 100, "espos test", "1.0", "rev A");
    n.SetDeviceInformation(unique, 140, 30, 2046, 4);
    n.SetMode(tNMEA2000::N2km_NodeOnly, address);
    n.EnableForward(false);
}

}  // namespace

extern "C" int64_t esp_timer_get_time(void)
{
    return g_now_us;
}

TEST_CASE("two nodes wanting one address: the lower NAME keeps it", "[node][bus]")
{
    g_bus.clear();
    SimNode low, high;
    /* Same everything but the unique number, which is in the NAME's low bits:
     * the lower NAME has priority. */
    setup(low, 1, 34);
    setup(high, 2, 34);
    run(2000, low, high);

    TEST_ASSERT_EQUAL_UINT8(34, low.GetN2kSource());
    TEST_ASSERT_NOT_EQUAL(34, high.GetN2kSource());
    TEST_ASSERT_TRUE(high.GetN2kSource() <= N2kMaxCanBusAddress);
    g_bus.clear();
}

TEST_CASE("an ISO request for product information gets the fast-packet answer", "[node][bus]")
{
    g_bus.clear();
    SimNode dev, mfd;
    setup(dev, 1, 34);
    setup(mfd, 2, 40);
    std::vector<unsigned long> seen;
    g_seen_by = &seen;
    mfd.SetMsgHandler(record);
    run(1000, dev, mfd);
    seen.clear();

    tN2kMsg req;
    SetN2kPGN59904(req, dev.GetN2kSource(), 126996);
    TEST_ASSERT_TRUE(mfd.SendMsg(req));
    run(500, dev, mfd);

    bool got = false;
    for (unsigned long pgn : seen) got |= pgn == 126996;
    TEST_ASSERT_TRUE_MESSAGE(got, "no assembled 126996 reached the requester");
    g_seen_by = nullptr;
    g_bus.clear();
}

TEST_CASE("a 127502 from the MFD arrives whole at the switch bank", "[node][bus]")
{
    g_bus.clear();
    SimNode bank, mfd;
    setup(bank, 1, 34);
    setup(mfd, 2, 40);
    static tN2kMsg last;
    static bool got;
    got = false;
    bank.SetMsgHandler([](const tN2kMsg &m) {
        if (m.PGN == 127502) {
            last = m;
            got = true;
        }
    });
    run(1000, bank, mfd);

    tN2kBinaryStatus cmd;
    N2kResetBinaryStatus(cmd);
    N2kSetStatusBinaryOnStatus(cmd, N2kOnOff_On, 3);
    tN2kMsg msg;
    SetN2kPGN127502(msg, 5, cmd);
    TEST_ASSERT_TRUE(mfd.SendMsg(msg));
    run(50, bank, mfd);

    TEST_ASSERT_TRUE(got);
    unsigned char inst = 0;
    tN2kBinaryStatus status;
    TEST_ASSERT_TRUE(ParseN2kSwitchbankControl(last, inst, status));
    TEST_ASSERT_EQUAL_UINT8(5, inst);
    TEST_ASSERT_EQUAL(N2kOnOff_On, N2kGetStatusOnBinaryStatus(status, 3));
    TEST_ASSERT_EQUAL(N2kOnOff_Unavailable, N2kGetStatusOnBinaryStatus(status, 1));
    g_bus.clear();
}
