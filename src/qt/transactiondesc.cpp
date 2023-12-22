// Copyright (c) 2011-2019 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifdef HAVE_CONFIG_H
#include <config/bitcoin-config.h>
#endif

#include <qt/transactiondesc.h>

#include <qt/bitcoinunits.h>
#include <qt/guiutil.h>
#include <qt/paymentserver.h>

#include <consensus/consensus.h>
#include <interfaces/node.h>
#include <interfaces/wallet.h>
#include <key_io.h>
#include <policy/policy.h>
#include <script/script.h>
#include <util/system.h>
#include <validation.h>
#include <wallet/ismine.h>
#include <wallet/txrecord.h>

#include <stdint.h>
#include <string>

QString TransactionDesc::FormatTxStatus(const interfaces::WalletTx& wtx, const interfaces::WalletTxStatus& status, bool inMempool, int numBlocks)
{
    if (!status.is_final)
    {
        if (wtx.tx->nLockTime < LOCKTIME_THRESHOLD)
            return tr("Open for %n more block(s)", "", wtx.tx->nLockTime - numBlocks);
        else
            return tr("Open until %1").arg(GUIUtil::dateTimeStr(wtx.tx->nLockTime));
    }
    else
    {
        int nDepth = status.depth_in_main_chain;
        if (nDepth < 0)
            return tr("conflicted with a transaction with %1 confirmations").arg(-nDepth);
        else if (nDepth == 0)
            return tr("0/unconfirmed, %1").arg((inMempool ? tr("in memory pool") : tr("not in memory pool"))) + (status.is_abandoned ? ", "+tr("abandoned") : "");
        else if (nDepth < 3)
            return tr("%1/unconfirmed").arg(nDepth);
        else
            return tr("%1 confirmations").arg(nDepth);
    }
}

// Takes an encoded PaymentRequest as a string and tries to find the Common Name of the X.509 certificate
// used to sign the PaymentRequest.
bool GetPaymentRequestMerchant(const std::string& pr, QString& merchant)
{
    // Search for the supported pki type strings
    if (pr.find(std::string({0x12, 0x0b}) + "x509+sha256") != std::string::npos || pr.find(std::string({0x12, 0x09}) + "x509+sha1") != std::string::npos) {
        // We want the common name of the Subject of the cert. This should be the second occurrence
        // of the bytes 0x0603550403. The first occurrence of those is the common name of the issuer.
        // After those bytes will be either 0x13 or 0x0C, then length, then either the ascii or utf8
        // string with the common name which is the merchant name
        size_t cn_pos = pr.find({0x06, 0x03, 0x55, 0x04, 0x03});
        if (cn_pos != std::string::npos) {
            cn_pos = pr.find({0x06, 0x03, 0x55, 0x04, 0x03}, cn_pos + 5);
            if (cn_pos != std::string::npos) {
                cn_pos += 5;
                if (pr[cn_pos] == 0x13 || pr[cn_pos] == 0x0c) {
                    cn_pos++; // Consume the type
                    int str_len = pr[cn_pos];
                    cn_pos++; // Consume the string length
                    merchant = QString::fromUtf8(pr.data() + cn_pos, str_len);
                    return true;
                }
            }
        }
    }
    return false;
}

QString TransactionDesc::toHTML(interfaces::Node& node, interfaces::Wallet& wallet, WalletTxRecord *rec, int unit)
{
    interfaces::WalletTxStatus status;
    interfaces::WalletOrderForm orderForm;
    bool inMempool;
    int numBlocks;
    interfaces::WalletTx wtx = wallet.getWalletTxDetails(rec->GetTxHash(), status, orderForm, inMempool, numBlocks);

    QString strHTML;

    strHTML.reserve(4000);
    strHTML += "<html><font face='verdana, arial, helvetica, sans-serif'>";

    int64_t nTime = wtx.time;
    strHTML += "<b>" + tr("Status") + ":</b> " + FormatTxStatus(wtx, status, inMempool, numBlocks) + "<br>";
    strHTML += "<b>" + tr("Date") + ":</b> " + (nTime ? GUIUtil::dateTimeStr(nTime) : "") + "<br>";

    strHTML += toHTML_Addresses(wallet, wtx, rec);
    strHTML += toHTML_Amounts(wallet, wtx, status, unit);

    //
    // Message
    //
    if (wtx.value_map.count("message") && !wtx.value_map["message"].empty())
        strHTML += "<br><b>" + tr("Message") + ":</b><br>" + GUIUtil::HtmlEscape(wtx.value_map["message"], true) + "<br>";
    if (wtx.value_map.count("comment") && !wtx.value_map["comment"].empty())
        strHTML += "<br><b>" + tr("Comment") + ":</b><br>" + GUIUtil::HtmlEscape(wtx.value_map["comment"], true) + "<br>";

    strHTML += "<b>" + tr("Transaction ID") + ":</b> " + QString::fromStdString(rec->GetTxHash().ToString()) + "<br>";

    if (!wtx.tx->IsNull()) {
        strHTML += "<b>" + tr("Transaction total size") + ":</b> " + QString::number(wtx.tx->GetTotalSize()) + " bytes<br>";
        strHTML += "<b>" + tr("Transaction virtual size") + ":</b> " + QString::number(GetVirtualTransactionSize(*wtx.tx)) + " bytes<br>";
    }

    if (wtx.tx->HasMWEBTx()) {
        strHTML += "<b>" + tr("Transaction MWEB weight") + ":</b> " + QString::number(wtx.tx->mweb_tx.GetMWEBWeight()) + "<br>";
    }
    strHTML += "<b>" + tr("Component index") + ":</b> " + QString::fromStdString(rec->GetComponentIndex()) + "<br>";

    strHTML += toHTML_OrderForm(orderForm);

    if (wtx.is_coinbase) // MW: TODO - Include pegout maturity
    {
        quint32 numBlocksToMaturity = COINBASE_MATURITY +  1;
        strHTML += "<br>" + tr("Generated coins must mature %1 blocks before they can be spent. When you generated this block, it was broadcast to the network to be added to the block chain. If it fails to get into the chain, its state will change to \"not accepted\" and it won't be spendable. This may occasionally happen if another node generates a block within a few seconds of yours.").arg(QString::number(numBlocksToMaturity)) + "<br>";
    }

    //
    // Debug view
    //
    if (node.getLogCategories() != BCLog::NONE)
    {
        strHTML += toHTML_Debug(node, wallet, wtx, rec, unit);
    }

    strHTML += "</font></html>";
    return strHTML;
}

QString TransactionDesc::toHTML_Addresses(interfaces::Wallet& wallet, const interfaces::WalletTx& wtx, WalletTxRecord* rec)
{
    QString strHTML;

    //
    // From
    //
    if (wtx.is_coinbase) {
        strHTML += "<b>" + tr("Source") + ":</b> " + tr("Generated") + "<br>";
    } else if (wtx.value_map.count("from") && !wtx.value_map.at("from").empty()) {
        // Online transaction
        strHTML += "<b>" + tr("From") + ":</b> " + GUIUtil::HtmlEscape(wtx.value_map.at("from")) + "<br>";
    } else {
        // Offline transaction

        CAmount nNet = wtx.credit - wtx.debit;
        if (nNet > 0) {
            // Credit
            CTxDestination address = DecodeDestination(rec->address);
            if (IsValidDestination(address)) {
                std::string name;
                isminetype ismine;
                if (wallet.getAddress(address, &name, &ismine, /* purpose= */ nullptr)) {
                    strHTML += "<b>" + tr("From") + ":</b> " + tr("unknown") + "<br>";
                    strHTML += "<b>" + tr("To") + ":</b> ";
                    strHTML += GUIUtil::HtmlEscape(rec->address);
                    QString addressOwned = ismine == ISMINE_SPENDABLE ? tr("own address") : tr("watch-only");
                    if (!name.empty())
                        strHTML += " (" + addressOwned + ", " + tr("label") + ": " + GUIUtil::HtmlEscape(name) + ")";
                    else
                        strHTML += " (" + addressOwned + ")";
                    strHTML += "<br>";
                }
            }
        }
    }

    //
    // To
    //
    if (wtx.value_map.count("to") && !wtx.value_map.at("to").empty()) {
        // Online transaction
        std::string strAddress = wtx.value_map.at("to");
        strHTML += "<b>" + tr("To") + ":</b> ";
        CTxDestination dest = DecodeDestination(strAddress);
        std::string name;
        if (wallet.getAddress(
                dest, &name, /* is_mine= */ nullptr, /* purpose= */ nullptr) &&
            !name.empty())
            strHTML += GUIUtil::HtmlEscape(name) + " ";
        strHTML += GUIUtil::HtmlEscape(strAddress) + "<br>";
    }

    return strHTML;
}

QString TransactionDesc::toHTML_Amounts(interfaces::Wallet& wallet, const interfaces::WalletTx& wtx, const interfaces::WalletTxStatus& status, int unit)
{
    QString strHTML;
    CAmount nNet = wtx.credit - wtx.debit;

    //
    // Amount
    //
    if (status.blocks_to_maturity > 0 && wtx.credit == 0) // Blocks to maturity covers coinbase and pegouts (HogEx)
    {
        //
        // Coinbase & Pegouts (HogEx)
        //
        CAmount nUnmatured = 0;
        for (const interfaces::WalletTxOut& out : wtx.outputs)
            nUnmatured += wallet.getCredit(out.txout, ISMINE_ALL);
        strHTML += "<b>" + tr("Credit") + ":</b> ";
        if (status.is_in_main_chain)
            strHTML += BitcoinUnits::formatHtmlWithUnit(unit, nUnmatured) + " (" + tr("matures in %n more block(s)", "", status.blocks_to_maturity) + ")";
        else
            strHTML += "(" + tr("not accepted") + ")";
        strHTML += "<br>";
    } else if (nNet > 0) {
        //
        // Credit
        //
        strHTML += "<b>" + tr("Credit") + ":</b> " + BitcoinUnits::formatHtmlWithUnit(unit, nNet) + "<br>";
    } else {
        isminetype fAllFromMe = ISMINE_SPENDABLE;
        for (const isminetype mine : wtx.txin_is_mine) {
            if (fAllFromMe > mine) fAllFromMe = mine;
        }

        isminetype fAllToMe = ISMINE_SPENDABLE;
        for (const isminetype mine : wtx.txout_is_mine) {
            if (fAllToMe > mine) fAllToMe = mine;
        }

        for (const isminetype mine : wtx.pegout_is_mine) {
            if (fAllToMe > mine) fAllToMe = mine;
        }

        if (fAllFromMe) {
            if (fAllFromMe & ISMINE_WATCH_ONLY)
                strHTML += "<b>" + tr("From") + ":</b> " + tr("watch-only") + "<br>";

            //
            // Debit
            //
            auto mine = wtx.txout_is_mine.begin();
            for (const interfaces::WalletTxOut& txout : wtx.outputs) {
                // Ignore change
                isminetype toSelf = *(mine++);
                if ((toSelf == ISMINE_SPENDABLE) && (fAllFromMe == ISMINE_SPENDABLE))
                    continue;

                if (!wtx.value_map.count("to") || wtx.value_map.at("to").empty()) {
                    // Offline transaction
                    CTxDestination dest;
                    if (txout.address.ExtractDestination(dest)) {
                        strHTML += "<b>" + tr("To") + ":</b> ";
                        std::string name;
                        if (wallet.getAddress(
                                dest, &name, /* is_mine= */ nullptr, /* purpose= */ nullptr) &&
                            !name.empty())
                            strHTML += GUIUtil::HtmlEscape(name) + " ";
                        strHTML += GUIUtil::HtmlEscape(txout.address.Encode());
                        if (toSelf == ISMINE_SPENDABLE)
                            strHTML += " (own address)";
                        else if (toSelf & ISMINE_WATCH_ONLY)
                            strHTML += " (watch-only)";
                        strHTML += "<br>";
                    }
                }

                strHTML += "<b>" + tr("Debit") + ":</b> " + BitcoinUnits::formatHtmlWithUnit(unit, -txout.nValue) + "<br>";
                if (toSelf)
                    strHTML += "<b>" + tr("Credit") + ":</b> " + BitcoinUnits::formatHtmlWithUnit(unit, txout.nValue) + "<br>";
            }

            auto pegout_mine = wtx.pegout_is_mine.begin();
            for (const PegOutCoin& pegout : wtx.pegouts) {
                isminetype toSelf = *(pegout_mine++);
                if ((toSelf == ISMINE_SPENDABLE) && (fAllFromMe == ISMINE_SPENDABLE))
                    continue;

                CTxDestination dest;
                if (::ExtractDestination(pegout.GetScriptPubKey(), dest)) {
                    strHTML += "<b>" + tr("To") + ":</b> ";
                    std::string name;
                    if (wallet.getAddress(
                            dest, &name, /* is_mine= */ nullptr, /* purpose= */ nullptr) &&
                        !name.empty())
                        strHTML += GUIUtil::HtmlEscape(name) + " ";
                    strHTML += GUIUtil::HtmlEscape(::EncodeDestination(dest));
                    if (toSelf == ISMINE_SPENDABLE)
                        strHTML += " (own address)";
                    else if (toSelf & ISMINE_WATCH_ONLY)
                        strHTML += " (watch-only)";
                    strHTML += "<br>";
                }

                strHTML += "<b>" + tr("Debit") + ":</b> " + BitcoinUnits::formatHtmlWithUnit(unit, -pegout.GetAmount()) + "<br>";
                if (toSelf)
                    strHTML += "<b>" + tr("Credit") + ":</b> " + BitcoinUnits::formatHtmlWithUnit(unit, pegout.GetAmount()) + "<br>";
            }

            if (fAllToMe) {
                // Payment to self
                CAmount nValue = wtx.credit - wtx.change;
                strHTML += "<b>" + tr("Credit") + ":</b> " + BitcoinUnits::formatHtmlWithUnit(unit, wtx.credit) + "<br>";
                strHTML += "<b>" + tr("Change") + ":</b> " + BitcoinUnits::formatHtmlWithUnit(unit, wtx.change) + "<br>";
                strHTML += "<b>" + tr("Total debit") + ":</b> " + BitcoinUnits::formatHtmlWithUnit(unit, -nValue) + "<br>";
                strHTML += "<b>" + tr("Total credit") + ":</b> " + BitcoinUnits::formatHtmlWithUnit(unit, nValue) + "<br>";
            }

            CAmount nTxFee = wtx.fee;
            if (nTxFee > 0)
                strHTML += "<b>" + tr("Transaction fee") + ":</b> " + BitcoinUnits::formatHtmlWithUnit(unit, -nTxFee) + "<br>";
        } else {
            //
            // Mixed debit transaction
            //
            auto mine = wtx.txin_is_mine.begin();
            for (const CTxInput& txin : wtx.inputs) {
                if (*(mine++)) {
                    strHTML += "<b>" + tr("Debit") + ":</b> " + BitcoinUnits::formatHtmlWithUnit(unit, -wallet.getDebit(txin, ISMINE_ALL)) + "<br>";
                }
            }

            mine = wtx.txout_is_mine.begin();
            for (const interfaces::WalletTxOut& out : wtx.outputs) {
                if (*(mine++)) {
                    strHTML += "<b>" + tr("Credit") + ":</b> " + BitcoinUnits::formatHtmlWithUnit(unit, wallet.getCredit(out.txout, ISMINE_ALL)) + "<br>";
                }
            }

            mine = wtx.pegout_is_mine.begin();
            for (const PegOutCoin& pegout : wtx.pegouts) {
                if (*(mine++)) {
                    strHTML += "<b>" + tr("Credit") + ":</b> " + BitcoinUnits::formatHtmlWithUnit(unit, pegout.GetAmount()) + "<br>";
                }
            }
        }
    }

    strHTML += "<b>" + tr("Net amount") + ":</b> " + BitcoinUnits::formatHtmlWithUnit(unit, nNet, true) + "<br>";
    return strHTML;
}

QString TransactionDesc::toHTML_OrderForm(const interfaces::WalletOrderForm& orderForm)
{
    QString strHTML;

    // Message from normal bitcoin:URI (bitcoin:123...?message=example)
    for (const std::pair<std::string, std::string>& r : orderForm) {
        if (r.first == "Message")
            strHTML += "<br><b>" + tr("Message") + ":</b><br>" + GUIUtil::HtmlEscape(r.second, true) + "<br>";

        //
        // PaymentRequest info:
        //
        if (r.first == "PaymentRequest") {
            QString merchant;
            if (!GetPaymentRequestMerchant(r.second, merchant)) {
                merchant.clear();
            } else {
                merchant += tr(" (Certificate was not verified)");
            }
            if (!merchant.isNull()) {
                strHTML += "<b>" + tr("Merchant") + ":</b> " + GUIUtil::HtmlEscape(merchant) + "<br>";
            }
        }
    }

    return strHTML;
}

QString TransactionDesc::toHTML_Debug(interfaces::Node& node, interfaces::Wallet& wallet, const interfaces::WalletTx& wtx, WalletTxRecord* rec, int unit)
{
    QString strHTML = "<hr><br>" + tr("Debug information") + "<br><br>";
    for (const CTxInput& txin : wtx.inputs) {
        if (wallet.txinIsMine(txin))
            strHTML += "<b>" + tr("Debit") + ":</b> " + BitcoinUnits::formatHtmlWithUnit(unit, -wallet.getDebit(txin, ISMINE_ALL)) + "<br>";
    }
    for (const interfaces::WalletTxOut& out : wtx.outputs)
        if (wallet.txoutIsMine(out.txout))
            strHTML += "<b>" + tr("Credit") + ":</b> " + BitcoinUnits::formatHtmlWithUnit(unit, wallet.getCredit(out.txout, ISMINE_ALL)) + "<br>";

    strHTML += "<br><b>" + tr("Transaction") + ":</b><br>";
    strHTML += GUIUtil::HtmlEscape(rec->GetTxString(), true);

    auto get_prev_out = [&](const CTxInput& txin, CTxOutput& prevout) -> bool {
        if (node.getUnspentOutput(txin.GetIndex(), prevout)) {
            return true;
        }

        if (txin.IsMWEB()) {
            prevout = CTxOutput{txin.ToMWEB()};
            return true;
        }

        return false;
    };

    strHTML += "<br><b>" + tr("Inputs") + ":</b>";
    strHTML += "<ul>";

    for (const CTxInput& txin : wtx.inputs) {
        strHTML += "<li>";

        CTxOutput prevout;
        if (get_prev_out(txin, prevout)) {
            CTxDestination address;
            if (wallet.extractOutputDestination(prevout, address)) {
                std::string name;
                if (wallet.getAddress(address, &name, /* is_mine= */ nullptr, /* purpose= */ nullptr) && !name.empty())
                    strHTML += GUIUtil::HtmlEscape(name) + " ";
                strHTML += QString::fromStdString(EncodeDestination(address));
            }

            strHTML = strHTML + " " + tr("Amount") + "=" + BitcoinUnits::formatHtmlWithUnit(unit, wallet.getValue(prevout));
        }

        strHTML = strHTML + " IsMine=" + (wallet.txinIsMine(txin) & ISMINE_SPENDABLE ? tr("true") : tr("false"));
        strHTML = strHTML + " IsWatchOnly=" + (wallet.txinIsMine(txin) & ISMINE_WATCH_ONLY ? tr("true") : tr("false"));
        strHTML += "</li>";
    }

    strHTML += "</ul>";

    
    strHTML += "<br><b>" + tr("Outputs") + ":</b>";
    strHTML += "<ul>";

    for (const interfaces::WalletTxOut& txout : wtx.outputs) {
        strHTML += "<li>";

        CTxDestination address;
        if (txout.address.ExtractDestination(address)) {
            std::string name;
            if (wallet.getAddress(address, &name, /* is_mine= */ nullptr, /* purpose= */ nullptr) && !name.empty())
                strHTML += GUIUtil::HtmlEscape(name) + " ";
            strHTML += QString::fromStdString(EncodeDestination(address));
        }

        strHTML = strHTML + " " + tr("Amount") + "=" + BitcoinUnits::formatHtmlWithUnit(unit, wallet.getValue(txout.txout));

        strHTML = strHTML + " IsMine=" + (wallet.txoutIsMine(txout.txout) & ISMINE_SPENDABLE ? tr("true") : tr("false"));
        strHTML = strHTML + " IsWatchOnly=" + (wallet.txoutIsMine(txout.txout) & ISMINE_WATCH_ONLY ? tr("true") : tr("false"));
        strHTML += "</li>";
    }

    strHTML += "</ul>";

    return strHTML;
}