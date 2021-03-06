// Copyright 2020 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "ledger/LedgerTxn.h"
#include "lib/catch.hpp"
#include "test/TestAccount.h"
#include "test/TestUtils.h"
#include "test/TxTests.h"
#include "test/test.h"
#include "transactions/TransactionFrameBase.h"
#include "transactions/TransactionUtils.h"
#include "transactions/test/SponsorshipTestUtils.h"

using namespace stellar;
using namespace stellar::txtest;

static RevokeSponsorshipResultCode
getUpdateSponsorshipResultCode(TransactionFrameBasePtr& tx, size_t i)
{
    auto const& opRes = tx->getResult().result.results()[i];
    return opRes.tr().revokeSponsorshipResult().code();
}

Claimant
getClaimant(TestAccount const& account)
{
    ClaimPredicate pred;
    pred.type(CLAIM_PREDICATE_BEFORE_ABSOLUTE_TIME).absBefore() = INT64_MAX;

    Claimant c;
    c.v0().destination = account;
    c.v0().predicate = pred;

    return c;
}

TEST_CASE("update sponsorship", "[tx][sponsorship]")
{
    VirtualClock clock;
    auto app = createTestApplication(clock, getTestConfig());
    app->start();

    auto minBal = [&](uint32_t n) {
        return app->getLedgerManager().getLastMinBalance(n);
    };

    auto root = TestAccount::createRoot(*app);

    SECTION("entry is not sponsored")
    {
        // No-op
        SECTION("account is not sponsored")
        {
            SECTION("account")
            {
                auto a1 = root.create("a1", minBal(1));
                auto tx = transactionFrameFromOps(
                    app->getNetworkID(), a1,
                    {a1.op(updateSponsorship(accountKey(a1)))}, {});

                LedgerTxn ltx(app->getLedgerTxnRoot());
                TransactionMeta txm(2);
                REQUIRE(tx->checkValid(ltx, 0, 0, 0));
                REQUIRE(tx->apply(*app, ltx, txm));

                checkSponsorship(ltx, accountKey(a1), 0, nullptr);
                checkSponsorship(ltx, a1, 0, nullptr, 0, 0, 0, 0);
                ltx.commit();
            }

            SECTION("trust line")
            {
                auto cur1 = makeAsset(root, "CUR1");
                auto a1 = root.create("a1", minBal(2));
                a1.changeTrust(cur1, 1000);
                auto tx = transactionFrameFromOps(
                    app->getNetworkID(), a1,
                    {a1.op(updateSponsorship(trustlineKey(a1, cur1)))}, {});

                LedgerTxn ltx(app->getLedgerTxnRoot());
                TransactionMeta txm(2);
                REQUIRE(tx->checkValid(ltx, 0, 0, 0));
                REQUIRE(tx->apply(*app, ltx, txm));

                checkSponsorship(ltx, trustlineKey(a1, cur1), 0, nullptr);
                checkSponsorship(ltx, a1, 0, nullptr, 1, 0, 0, 0);
                ltx.commit();
            }

            SECTION("signer")
            {
                auto cur1 = makeAsset(root, "CUR1");
                auto a1 = root.create("a1", minBal(2));
                auto signer = makeSigner(getAccount("S1"), 1);
                a1.setOptions(setSigner(signer));
                auto tx = transactionFrameFromOps(
                    app->getNetworkID(), a1,
                    {a1.op(updateSponsorship(a1, signer.key))}, {});

                LedgerTxn ltx(app->getLedgerTxnRoot());
                TransactionMeta txm(2);
                REQUIRE(tx->checkValid(ltx, 0, 0, 0));
                REQUIRE(tx->apply(*app, ltx, txm));

                checkSponsorship(ltx, a1, signer.key, 0, nullptr);
                checkSponsorship(ltx, a1, 0, nullptr, 1, 0, 0, 0);
                ltx.commit();
            }

            SECTION("claimable balances")
            {
                auto native = makeNativeAsset();
                auto a1 = root.create("a1", minBal(2));

                auto balanceID =
                    a1.createClaimableBalance(native, 1, {getClaimant(a1)});

                auto tx = transactionFrameFromOps(
                    app->getNetworkID(), a1,
                    {a1.op(updateSponsorship(claimableBalanceKey(balanceID)))},
                    {});

                LedgerTxn ltx(app->getLedgerTxnRoot());
                TransactionMeta txm(2);
                REQUIRE(tx->checkValid(ltx, 0, 0, 0));
                REQUIRE(!tx->apply(*app, ltx, txm));

                REQUIRE(getUpdateSponsorshipResultCode(tx, 0) ==
                        REVOKE_SPONSORSHIP_ONLY_TRANSFERABLE);

                checkSponsorship(ltx, claimableBalanceKey(balanceID), 1,
                                 &a1.getPublicKey());
                checkSponsorship(ltx, a1, 0, nullptr, 0, 2, 1, 0);
                ltx.commit();
            }
        }

        // Transfers sponsorship to sponsor-of-account
        SECTION("account is sponsored")
        {
            SECTION("account")
            {
                auto a1 = root.create("a1", minBal(1));

                auto tx = transactionFrameFromOps(
                    app->getNetworkID(), a1,
                    {root.op(sponsorFutureReserves(a1)),
                     a1.op(updateSponsorship(accountKey(a1))),
                     a1.op(confirmAndClearSponsor())},
                    {root});

                LedgerTxn ltx(app->getLedgerTxnRoot());
                TransactionMeta txm(2);
                REQUIRE(tx->checkValid(ltx, 0, 0, 0));
                REQUIRE(tx->apply(*app, ltx, txm));

                checkSponsorship(ltx, accountKey(a1), 1, &root.getPublicKey());
                checkSponsorship(ltx, a1, 1, &root.getPublicKey(), 0, 2, 0, 2);
                ltx.commit();
            }
            SECTION("trust line")
            {
                auto cur1 = makeAsset(root, "CUR1");
                auto a1 = root.create("a1", minBal(2));
                a1.changeTrust(cur1, 1000);
                auto tx = transactionFrameFromOps(
                    app->getNetworkID(), a1,
                    {root.op(sponsorFutureReserves(a1)),
                     a1.op(updateSponsorship(trustlineKey(a1, cur1))),
                     a1.op(confirmAndClearSponsor())},
                    {root});

                LedgerTxn ltx(app->getLedgerTxnRoot());
                TransactionMeta txm(2);
                REQUIRE(tx->checkValid(ltx, 0, 0, 0));
                REQUIRE(tx->apply(*app, ltx, txm));

                checkSponsorship(ltx, trustlineKey(a1, cur1), 1,
                                 &root.getPublicKey());
                checkSponsorship(ltx, a1, 0, nullptr, 1, 2, 0, 1);
                checkSponsorship(ltx, root, 0, nullptr, 0, 2, 1, 0);
                ltx.commit();
            }

            SECTION("signer")
            {
                auto cur1 = makeAsset(root, "CUR1");
                auto a1 = root.create("a1", minBal(2));
                auto signer = makeSigner(getAccount("S1"), 1);
                a1.setOptions(setSigner(signer));
                auto tx = transactionFrameFromOps(
                    app->getNetworkID(), a1,
                    {root.op(sponsorFutureReserves(a1)),
                     a1.op(updateSponsorship(a1, signer.key)),
                     a1.op(confirmAndClearSponsor())},
                    {root});

                LedgerTxn ltx(app->getLedgerTxnRoot());
                TransactionMeta txm(2);
                REQUIRE(tx->checkValid(ltx, 0, 0, 0));
                REQUIRE(tx->apply(*app, ltx, txm));

                checkSponsorship(ltx, a1, signer.key, 2, &root.getPublicKey());
                checkSponsorship(ltx, a1, 0, nullptr, 1, 2, 0, 1);
                checkSponsorship(ltx, root, 0, nullptr, 0, 2, 1, 0);
                ltx.commit();
            }

            SECTION("claimable balances")
            {
                auto native = makeNativeAsset();
                auto a1 = root.create("a1", minBal(2));

                auto balanceID =
                    a1.createClaimableBalance(native, 1, {getClaimant(a1)});

                auto tx = transactionFrameFromOps(
                    app->getNetworkID(), a1,
                    {root.op(sponsorFutureReserves(a1)),
                     a1.op(updateSponsorship(claimableBalanceKey(balanceID))),
                     a1.op(confirmAndClearSponsor())},
                    {root});

                LedgerTxn ltx(app->getLedgerTxnRoot());
                TransactionMeta txm(2);
                REQUIRE(tx->checkValid(ltx, 0, 0, 0));
                REQUIRE(tx->apply(*app, ltx, txm));

                checkSponsorship(ltx, claimableBalanceKey(balanceID), 1,
                                 &root.getPublicKey());
                checkSponsorship(ltx, a1, 0, nullptr, 0, 2, 0, 0);
                ltx.commit();
            }
        }
    }

    SECTION("entry is sponsored")
    {
        // Revokes sponsorship entirely
        SECTION("sponsor is not sponsored")
        {
            SECTION("account")
            {
                auto key = getAccount("a1");
                TestAccount a1(*app, key);
                auto tx1 = transactionFrameFromOps(
                    app->getNetworkID(), root,
                    {root.op(sponsorFutureReserves(a1)),
                     root.op(createAccount(a1, minBal(2))),
                     a1.op(confirmAndClearSponsor())},
                    {key});

                LedgerTxn ltx(app->getLedgerTxnRoot());
                TransactionMeta txm1(2);
                REQUIRE(tx1->checkValid(ltx, 0, 0, 0));
                REQUIRE(tx1->apply(*app, ltx, txm1));

                auto tx2 = transactionFrameFromOps(
                    app->getNetworkID(), root,
                    {root.op(updateSponsorship(accountKey(a1.getPublicKey())))},
                    {});

                TransactionMeta txm2(2);
                REQUIRE(tx2->checkValid(ltx, 0, 0, 0));
                REQUIRE(tx2->apply(*app, ltx, txm2));

                checkSponsorship(ltx, accountKey(a1.getPublicKey()), 1,
                                 nullptr);
                checkSponsorship(ltx, a1, 1, nullptr, 0, 2, 0, 0);
                checkSponsorship(ltx, root, 0, nullptr, 0, 2, 0, 0);
                ltx.commit();
            }

            SECTION("trust line")
            {
                auto cur1 = makeAsset(root, "CUR1");
                auto a1 = root.create("a1", minBal(1));

                auto tx1 =
                    transactionFrameFromOps(app->getNetworkID(), root,
                                            {root.op(sponsorFutureReserves(a1)),
                                             a1.op(changeTrust(cur1, 1000)),
                                             a1.op(confirmAndClearSponsor())},
                                            {a1});

                LedgerTxn ltx(app->getLedgerTxnRoot());
                TransactionMeta txm1(2);
                REQUIRE(tx1->checkValid(ltx, 0, 0, 0));
                REQUIRE(tx1->apply(*app, ltx, txm1));

                auto tx2 = transactionFrameFromOps(
                    app->getNetworkID(), root,
                    {root.op(updateSponsorship(trustlineKey(a1, cur1)))}, {});

                TransactionMeta txm2(2);
                REQUIRE(tx2->checkValid(ltx, 0, 0, 0));
                REQUIRE(tx2->apply(*app, ltx, txm2));

                checkSponsorship(ltx, trustlineKey(a1, cur1), 1, nullptr);
                checkSponsorship(ltx, a1, 0, nullptr, 1, 2, 0, 0);
                checkSponsorship(ltx, root, 0, nullptr, 0, 2, 0, 0);
                ltx.commit();
            }

            SECTION("signer")
            {
                auto a1 = root.create("a1", minBal(1));

                auto signer = makeSigner(getAccount("S1"), 1);
                auto tx1 = transactionFrameFromOps(
                    app->getNetworkID(), root,
                    {root.op(sponsorFutureReserves(a1)),
                     a1.op(setOptions(setSigner(signer))),
                     a1.op(confirmAndClearSponsor())},
                    {a1});

                LedgerTxn ltx(app->getLedgerTxnRoot());
                TransactionMeta txm1(2);
                REQUIRE(tx1->checkValid(ltx, 0, 0, 0));
                REQUIRE(tx1->apply(*app, ltx, txm1));

                auto tx2 = transactionFrameFromOps(
                    app->getNetworkID(), root,
                    {root.op(updateSponsorship(a1, signer.key))}, {});

                TransactionMeta txm2(2);
                REQUIRE(tx2->checkValid(ltx, 0, 0, 0));
                REQUIRE(tx2->apply(*app, ltx, txm2));

                checkSponsorship(ltx, a1, signer.key, 2, nullptr);
                checkSponsorship(ltx, a1, 0, nullptr, 1, 2, 0, 0);
                checkSponsorship(ltx, root, 0, nullptr, 0, 2, 0, 0);
                ltx.commit();
            }

            SECTION("claimable balance")
            {
                auto native = makeNativeAsset();
                auto a1 = root.create("a1", minBal(2));

                auto tx1 =
                    transactionFrameFromOps(app->getNetworkID(), root,
                                            {root.op(sponsorFutureReserves(a1)),
                                             a1.op(createClaimableBalance(
                                                 native, 1, {getClaimant(a1)})),
                                             a1.op(confirmAndClearSponsor())},
                                            {a1});

                LedgerTxn ltx(app->getLedgerTxnRoot());
                TransactionMeta txm1(2);
                REQUIRE(tx1->checkValid(ltx, 0, 0, 0));
                REQUIRE(tx1->apply(*app, ltx, txm1));

                auto balanceID = tx1->getResult()
                                     .result.results()[1]
                                     .tr()
                                     .createClaimableBalanceResult()
                                     .balanceID();

                auto tx2 = transactionFrameFromOps(
                    app->getNetworkID(), root,
                    {root.op(
                        updateSponsorship(claimableBalanceKey(balanceID)))},
                    {});

                TransactionMeta txm2(2);
                REQUIRE(tx2->checkValid(ltx, 0, 0, 0));
                REQUIRE(!tx2->apply(*app, ltx, txm2));

                REQUIRE(getUpdateSponsorshipResultCode(tx2, 0) ==
                        REVOKE_SPONSORSHIP_ONLY_TRANSFERABLE);

                checkSponsorship(ltx, claimableBalanceKey(balanceID), 1,
                                 &root.getPublicKey());
                checkSponsorship(ltx, a1, 0, nullptr, 0, 0, 0, 0);
                ltx.commit();
            }
        }

        // Transfers sponsorship to sponsor-of-sponsor
        SECTION("sponsor is sponsored")
        {
            SECTION("account")
            {
                auto a2 = root.create("a2", minBal(2));
                auto key = getAccount("a1");
                TestAccount a1(*app, key);

                auto tx1 = transactionFrameFromOps(
                    app->getNetworkID(), root,
                    {root.op(sponsorFutureReserves(a1)),
                     root.op(createAccount(a1, minBal(2))),
                     a1.op(confirmAndClearSponsor())},
                    {key});

                LedgerTxn ltx(app->getLedgerTxnRoot());
                TransactionMeta txm1(2);
                REQUIRE(tx1->checkValid(ltx, 0, 0, 0));
                REQUIRE(tx1->apply(*app, ltx, txm1));

                auto tx2 = transactionFrameFromOps(
                    app->getNetworkID(), root,
                    {a2.op(sponsorFutureReserves(root)),
                     root.op(updateSponsorship(accountKey(a1.getPublicKey()))),
                     root.op(confirmAndClearSponsor())},
                    {a2});

                TransactionMeta txm2(2);
                REQUIRE(tx2->checkValid(ltx, 0, 0, 0));
                REQUIRE(tx2->apply(*app, ltx, txm2));

                checkSponsorship(ltx, a1, 1, &a2.getPublicKey(), 0, 2, 0, 2);
                checkSponsorship(ltx, a2, 0, nullptr, 0, 2, 2, 0);
                ltx.commit();
            }

            SECTION("trust line")
            {
                auto cur1 = makeAsset(root, "CUR1");
                auto a1 = root.create("a1", minBal(0));
                auto a2 = root.create("a2", minBal(2));

                auto tx1 =
                    transactionFrameFromOps(app->getNetworkID(), root,
                                            {root.op(sponsorFutureReserves(a1)),
                                             a1.op(changeTrust(cur1, 1000)),
                                             a1.op(confirmAndClearSponsor())},
                                            {a1});

                LedgerTxn ltx(app->getLedgerTxnRoot());
                TransactionMeta txm1(2);
                REQUIRE(tx1->checkValid(ltx, 0, 0, 0));
                REQUIRE(tx1->apply(*app, ltx, txm1));

                auto tx2 = transactionFrameFromOps(
                    app->getNetworkID(), root,
                    {a2.op(sponsorFutureReserves(root)),
                     root.op(updateSponsorship(trustlineKey(a1, cur1))),
                     root.op(confirmAndClearSponsor())},
                    {a2});

                TransactionMeta txm2(2);
                REQUIRE(tx2->checkValid(ltx, 0, 0, 0));
                REQUIRE(tx2->apply(*app, ltx, txm2));

                checkSponsorship(ltx, trustlineKey(a1, cur1), 1,
                                 &a2.getPublicKey());
                checkSponsorship(ltx, a1, 0, nullptr, 1, 2, 0, 1);
                checkSponsorship(ltx, a2, 0, nullptr, 0, 2, 1, 0);
                checkSponsorship(ltx, root, 0, nullptr, 0, 2, 0, 0);
                ltx.commit();
            }

            SECTION("signer")
            {
                auto a1 = root.create("a1", minBal(1));
                auto a2 = root.create("a2", minBal(2));

                auto signer = makeSigner(getAccount("S1"), 1);
                auto tx1 = transactionFrameFromOps(
                    app->getNetworkID(), root,
                    {root.op(sponsorFutureReserves(a1)),
                     a1.op(setOptions(setSigner(signer))),
                     a1.op(confirmAndClearSponsor())},
                    {a1});

                LedgerTxn ltx(app->getLedgerTxnRoot());
                TransactionMeta txm1(2);
                REQUIRE(tx1->checkValid(ltx, 0, 0, 0));
                REQUIRE(tx1->apply(*app, ltx, txm1));
                auto tx2 = transactionFrameFromOps(
                    app->getNetworkID(), root,
                    {a2.op(sponsorFutureReserves(root)),
                     root.op(updateSponsorship(a1, signer.key)),
                     root.op(confirmAndClearSponsor())},
                    {a2});

                TransactionMeta txm2(2);
                REQUIRE(tx2->checkValid(ltx, 0, 0, 0));
                REQUIRE(tx2->apply(*app, ltx, txm2));

                checkSponsorship(ltx, a1, signer.key, 2, &a2.getPublicKey());
                checkSponsorship(ltx, a1, 0, nullptr, 1, 2, 0, 1);
                checkSponsorship(ltx, a2, 0, nullptr, 0, 2, 1, 0);
                checkSponsorship(ltx, root, 0, nullptr, 0, 2, 0, 0);
                ltx.commit();
            }

            SECTION("claimable balances")
            {
                auto native = makeNativeAsset();
                auto a1 = root.create("a1", minBal(2));
                auto a2 = root.create("a2", minBal(2));

                auto tx1 =
                    transactionFrameFromOps(app->getNetworkID(), root,
                                            {root.op(sponsorFutureReserves(a1)),
                                             a1.op(createClaimableBalance(
                                                 native, 1, {getClaimant(a1)})),
                                             a1.op(confirmAndClearSponsor())},
                                            {a1});

                LedgerTxn ltx(app->getLedgerTxnRoot());
                TransactionMeta txm1(2);
                REQUIRE(tx1->checkValid(ltx, 0, 0, 0));
                REQUIRE(tx1->apply(*app, ltx, txm1));

                auto balanceID = tx1->getResult()
                                     .result.results()[1]
                                     .tr()
                                     .createClaimableBalanceResult()
                                     .balanceID();

                auto tx2 = transactionFrameFromOps(
                    app->getNetworkID(), root,
                    {a2.op(sponsorFutureReserves(root)),
                     root.op(updateSponsorship(claimableBalanceKey(balanceID))),
                     root.op(confirmAndClearSponsor())},
                    {a2});

                TransactionMeta txm2(2);
                REQUIRE(tx2->checkValid(ltx, 0, 0, 0));
                REQUIRE(tx2->apply(*app, ltx, txm2));

                checkSponsorship(ltx, claimableBalanceKey(balanceID), 1,
                                 &a2.getPublicKey());
                checkSponsorship(ltx, a1, 0, nullptr, 0, 0, 0, 0);
                checkSponsorship(ltx, a2, 0, nullptr, 0, 2, 1, 0);
                ltx.commit();
            }
        }
    }

    SECTION("too many sponsoring")
    {
        auto native = makeNativeAsset();
        auto a1 = root.create("a1", minBal(3));

        SECTION("account")
        {
            auto a2 = root.create("a2", minBal(3));
            tooManySponsoring(*app, a2, a1,
                              a2.op(updateSponsorship(accountKey(a2))),
                              a1.op(updateSponsorship(accountKey(a1))));
        }

        SECTION("signer")
        {
            auto signer1 = makeSigner(getAccount("S1"), 1);
            auto signer2 = makeSigner(getAccount("S2"), 1);
            a1.setOptions(setSigner(signer1));
            a1.setOptions(setSigner(signer2));

            tooManySponsoring(*app, a1,
                              a1.op(updateSponsorship(a1, signer1.key)),
                              a1.op(updateSponsorship(a1, signer2.key)));
        }

        SECTION("trustline")
        {
            auto cur1 = makeAsset(root, "CUR1");
            auto cur2 = makeAsset(root, "CUR2");
            a1.changeTrust(cur1, 1000);
            a1.changeTrust(cur2, 1000);

            tooManySponsoring(*app, a1,
                              a1.op(updateSponsorship(trustlineKey(a1, cur2))),
                              a1.op(updateSponsorship(trustlineKey(a1, cur1))));
        }
        SECTION("claimable balance")
        {
            auto id1 = a1.createClaimableBalance(native, 1, {getClaimant(a1)});
            auto id2 = a1.createClaimableBalance(native, 1, {getClaimant(a1)});

            tooManySponsoring(
                *app, a1, a1.op(updateSponsorship(claimableBalanceKey(id1))),
                a1.op(updateSponsorship(claimableBalanceKey(id2))));
        }
    }
}
