#include <qt/test/wallettests.h>
#include <qt/test/util.h>

#include <init.h>
#include <interfaces/chain.h>
#include <interfaces/node.h>
#include <base58.h>
#include <qt/bitcoinamountfield.h>
#include <qt/optionsmodel.h>
#include <qt/platformstyle.h>
#include <qt/qvalidatedlineedit.h>
#include <qt/sendcoinsdialog.h>
#include <qt/sendcoinsentry.h>
#include <qt/transactiontablemodel.h>
#include <qt/transactionfilterproxy.h>
#include <qt/blocknettransactionhistory.h>
#include <qt/transactionview.h>
#include <QSignalSpy>
#include <qt/walletmodel.h>
#include <key_io.h>
#include <test/test_bitcoin.h>
#include <validation.h>
#include <wallet/wallet.h>
#include <qt/overviewpage.h>
#include <qt/receivecoinsdialog.h>
#include <qt/recentrequeststablemodel.h>
#include <qt/receiverequestdialog.h>

#include <memory>

#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>
#include <QTextEdit>
#include <QListView>
#include <QDialogButtonBox>

namespace
{
//! Press "Yes" or "Cancel" buttons in modal send confirmation dialog.
void ConfirmSend(QString* text = nullptr, bool cancel = false)
{
    QTimer::singleShot(0, [text, cancel]() {
        for (QWidget* widget : QApplication::topLevelWidgets()) {
            if (widget->inherits("SendConfirmationDialog")) {
                SendConfirmationDialog* dialog = qobject_cast<SendConfirmationDialog*>(widget);
                if (text) *text = dialog->text();
                QAbstractButton* button = dialog->button(cancel ? QMessageBox::Cancel : QMessageBox::Yes);
                button->setEnabled(true);
                button->click();
            }
        }
    });
}

//! Send coins to address and return txid.
uint256 SendCoins(CWallet& wallet, SendCoinsDialog& sendCoinsDialog, const CTxDestination& address, CAmount amount, bool rbf)
{
    QVBoxLayout* entries = sendCoinsDialog.findChild<QVBoxLayout*>("entries");
    SendCoinsEntry* entry = qobject_cast<SendCoinsEntry*>(entries->itemAt(0)->widget());
    entry->findChild<QValidatedLineEdit*>("payTo")->setText(QString::fromStdString(EncodeDestination(address)));
    entry->findChild<BitcoinAmountField*>("payAmount")->setValue(amount);
    sendCoinsDialog.findChild<QFrame*>("frameFee")
        ->findChild<QFrame*>("frameFeeSelection")
        ->findChild<QCheckBox*>("optInRBF")
        ->setCheckState(rbf ? Qt::Checked : Qt::Unchecked);
    uint256 txid;
    boost::signals2::scoped_connection c(wallet.NotifyTransactionChanged.connect([&txid](CWallet*, const uint256& hash, ChangeType status) {
        if (status == CT_NEW) txid = hash;
    }));
    ConfirmSend();
    bool invoked = QMetaObject::invokeMethod(&sendCoinsDialog, "on_sendButton_clicked");
    assert(invoked);
    return txid;
}

//! Find index of txid in transaction list.
QModelIndex FindTx(const QAbstractItemModel& model, const uint256& txid)
{
    QString hash = QString::fromStdString(txid.ToString());
    int rows = model.rowCount({});
    for (int row = 0; row < rows; ++row) {
        QModelIndex index = model.index(row, 0, {});
        if (model.data(index, TransactionTableModel::TxHashRole) == hash) {
            return index;
        }
    }
    return {};
}

//! Invoke bumpfee on txid and check results.
void BumpFee(TransactionView& view, const uint256& txid, bool expectDisabled, std::string expectError, bool cancel)
{
    QTableView* table = view.findChild<QTableView*>("transactionView");
    QModelIndex index = FindTx(*table->selectionModel()->model(), txid);
    QVERIFY2(index.isValid(), "Could not find BumpFee txid");

    // Select row in table, invoke context menu, and make sure bumpfee action is
    // enabled or disabled as expected.
    QAction* action = view.findChild<QAction*>("bumpFeeAction");
    table->selectionModel()->select(index, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
    action->setEnabled(expectDisabled);
    table->customContextMenuRequested({});
    QCOMPARE(action->isEnabled(), !expectDisabled);

    action->setEnabled(true);
    QString text;
    if (expectError.empty()) {
        ConfirmSend(&text, cancel);
    } else {
        ConfirmMessage(&text);
    }
    action->trigger();
    QVERIFY(text.indexOf(QString::fromStdString(expectError)) != -1);
}

//! Add a plain (non-coinbase, non-coinstake) payment to the wallet by hand
//! and return its txid. Needed because Blocknet's decomposeTransaction skips
//! PoW coinbase outputs (it tracks coinstakes instead), so mined fixture
//! blocks never produce table rows; hand-added payments exercise the real
//! decompose/filter/confirm paths without mining, signing, or maturing.
//! The caller supplies nTimeReceived: sharing one timestamp across payments
//! keeps same-second boundary assertions deterministic (no second-boundary
//! straddle between adds).
uint256 AddPaymentTx(CWallet& wallet, const CKey& key, CAmount amount, int64_t nTimeReceived)
{
    CMutableTransaction mtx;
    mtx.vout.emplace_back(amount, GetScriptForRawPubKey(key.GetPubKey()));
    CWalletTx wtx(&wallet, MakeTransactionRef(mtx));
    wtx.nTimeReceived = nTimeReceived;
    LOCK(wallet.cs_wallet);
    wallet.LoadToWallet(wtx);
    assert(wallet.mapWallet.count(wtx.GetHash()) == 1);
    return wtx.GetHash();
}

//! Simple qt wallet tests.
//
// Test widgets can be debugged interactively calling show() on them and
// manually running the event loop, e.g.:
//
//     sendCoinsDialog.show();
//     QEventLoop().exec();
//
// This also requires overriding the default minimal Qt platform:
//
//     src/qt/test/test_bitcoin-qt -platform xcb      # Linux
//     src/qt/test/test_bitcoin-qt -platform windows  # Windows
//     src/qt/test/test_bitcoin-qt -platform cocoa    # macOS
void TestGUI()
{
    // Set up wallet and chain with TESTCHAIN_BLOCK_COUNT + 5 blocks (5 mature blocks for spending).
    TestChain100Setup test;
    for (int i = 0; i < 5; ++i) {
        test.CreateAndProcessBlock({}, GetScriptForRawPubKey(test.coinbaseKey.GetPubKey()));
    }
    auto chain = interfaces::MakeChain();
    std::shared_ptr<CWallet> wallet = std::make_shared<CWallet>(*chain, WalletLocation(), WalletDatabase::CreateMock());
    bool firstRun;
    wallet->LoadWallet(firstRun);
    {
        LOCK(wallet->cs_wallet);
        wallet->SetAddressBook(GetDestinationForKey(test.coinbaseKey.GetPubKey(), wallet->m_default_address_type), "", "receive");
        wallet->AddKeyPubKey(test.coinbaseKey, test.coinbaseKey.GetPubKey());
    }
    {
        auto locked_chain = wallet->chain().lock();
        WalletRescanReserver reserver(wallet.get());
        reserver.reserve();
        CWallet::ScanResult result = wallet->ScanForWalletTransactions(locked_chain->getBlockHash(0), {} /* stop_block */, reserver, true /* fUpdate */);
        QCOMPARE(result.status, CWallet::ScanResult::SUCCESS);
        QCOMPARE(result.last_scanned_block, chainActive.Tip()->GetBlockHash());
        QVERIFY(result.last_failed_block.IsNull());
    }
    wallet->SetBroadcastTransactions(true);

    // Create widgets for sending coins and listing transactions.
    std::unique_ptr<const PlatformStyle> platformStyle(PlatformStyle::instantiate("other"));
    SendCoinsDialog sendCoinsDialog(platformStyle.get());
    TransactionView transactionView(platformStyle.get());
    auto node = interfaces::MakeNode();
    OptionsModel optionsModel(*node);
    AddWallet(wallet);
    WalletModel walletModel(std::move(node->getWallets().back()), *node, platformStyle.get(), &optionsModel);
    RemoveWallet(wallet);
    sendCoinsDialog.setModel(&walletModel);
    transactionView.setModel(&walletModel);

    // Send two transactions, and verify they are added to transaction list.
    TransactionTableModel* transactionTableModel = walletModel.getTransactionTableModel();
    QCOMPARE(transactionTableModel->rowCount({}), TESTCHAIN_BLOCK_COUNT + 5);
    uint256 txid1 = SendCoins(*wallet.get(), sendCoinsDialog, CKeyID(), 5 * COIN, false /* rbf */);
    uint256 txid2 = SendCoins(*wallet.get(), sendCoinsDialog, CKeyID(), 10 * COIN, true /* rbf */);
    QCOMPARE(transactionTableModel->rowCount({}), TESTCHAIN_BLOCK_COUNT + 7);
    QVERIFY(FindTx(*transactionTableModel, txid1).isValid());
    QVERIFY(FindTx(*transactionTableModel, txid2).isValid());

    // Call bumpfee. Test disabled, canceled, enabled, then failing cases.
    BumpFee(transactionView, txid1, true /* expect disabled */, "not BIP 125 replaceable" /* expected error */, false /* cancel */);
    BumpFee(transactionView, txid2, false /* expect disabled */, {} /* expected error */, true /* cancel */);
    BumpFee(transactionView, txid2, false /* expect disabled */, {} /* expected error */, false /* cancel */);
    BumpFee(transactionView, txid2, true /* expect disabled */, "already bumped" /* expected error */, false /* cancel */);

    // Check current balance on OverviewPage
    OverviewPage overviewPage(platformStyle.get());
    overviewPage.setWalletModel(&walletModel);
    QLabel* balanceLabel = overviewPage.findChild<QLabel*>("labelBalance");
    QString balanceText = balanceLabel->text();
    int unit = walletModel.getOptionsModel()->getDisplayUnit();
    CAmount balance = walletModel.wallet().getBalance();
    QString balanceComparison = BitcoinUnits::formatWithUnit(unit, balance, false, BitcoinUnits::separatorAlways);
    QCOMPARE(balanceText, balanceComparison);

    // Check Request Payment button
    ReceiveCoinsDialog receiveCoinsDialog(platformStyle.get());
    receiveCoinsDialog.setModel(&walletModel);
    RecentRequestsTableModel* requestTableModel = walletModel.getRecentRequestsTableModel();

    // Label input
    QLineEdit* labelInput = receiveCoinsDialog.findChild<QLineEdit*>("reqLabel");
    labelInput->setText("TEST_LABEL_1");

    // Amount input
    BitcoinAmountField* amountInput = receiveCoinsDialog.findChild<BitcoinAmountField*>("reqAmount");
    amountInput->setValue(1);

    // Message input
    QLineEdit* messageInput = receiveCoinsDialog.findChild<QLineEdit*>("reqMessage");
    messageInput->setText("TEST_MESSAGE_1");
    int initialRowCount = requestTableModel->rowCount({});
    QPushButton* requestPaymentButton = receiveCoinsDialog.findChild<QPushButton*>("receiveButton");
    requestPaymentButton->click();
    for (QWidget* widget : QApplication::topLevelWidgets()) {
        if (widget->inherits("ReceiveRequestDialog")) {
            ReceiveRequestDialog* receiveRequestDialog = qobject_cast<ReceiveRequestDialog*>(widget);
            QTextEdit* rlist = receiveRequestDialog->QObject::findChild<QTextEdit*>("outUri");
            QString paymentText = rlist->toPlainText();
            QStringList paymentTextList = paymentText.split('\n');
            QCOMPARE(paymentTextList.at(0), QString("Payment information"));
            QVERIFY(paymentTextList.at(1).indexOf(QString("URI: blocknet:")) != -1);
            QVERIFY(paymentTextList.at(2).indexOf(QString("Address:")) != -1);
            QCOMPARE(paymentTextList.at(3), QString("Amount: 0.00000001 ") + QString::fromStdString(CURRENCY_UNIT));
            QCOMPARE(paymentTextList.at(4), QString("Label: TEST_LABEL_1"));
            QCOMPARE(paymentTextList.at(5), QString("Message: TEST_MESSAGE_1"));
        }
    }

    // Clear button
    QPushButton* clearButton = receiveCoinsDialog.findChild<QPushButton*>("clearButton");
    clearButton->click();
    QCOMPARE(labelInput->text(), QString(""));
    QCOMPARE(amountInput->value(), CAmount(0));
    QCOMPARE(messageInput->text(), QString(""));

    // Check addition to history
    int currentRowCount = requestTableModel->rowCount({});
    QCOMPARE(currentRowCount, initialRowCount+1);

    // Check Remove button
    QTableView* table = receiveCoinsDialog.findChild<QTableView*>("recentRequestsView");
    table->selectRow(currentRowCount-1);
    QPushButton* removeRequestButton = receiveCoinsDialog.findChild<QPushButton*>("removeRequestButton");
    removeRequestButton->click();
    QCOMPARE(requestTableModel->rowCount({}), currentRowCount-1);
}

} // namespace

void WalletTests::filterDateTests()
{
#ifdef Q_OS_MAC
    if (QApplication::platformName() == "minimal") {
        QWARN("Skipping filterDateTests on mac build with 'minimal' platform (QTBUG-49686).");
        return;
    }
#endif
    // Hermetic fixture: plain TestingSetup (no mining) + hand-added payments.
    // Mined coinbases would yield zero rows here (Blocknet decompose skips
    // PoW coinbase outputs), so payments fund the model instead. All payments
    // land in the same second, which keeps boundary assertions deterministic:
    // a single-point range contains every row, a ±1s range contains none.
    TestingSetup test;
    auto chain = interfaces::MakeChain();
    std::shared_ptr<CWallet> wallet = std::make_shared<CWallet>(*chain, WalletLocation(), WalletDatabase::CreateMock());
    bool firstRun;
    wallet->LoadWallet(firstRun);
    CKey key;
    key.MakeNewKey(true);
    {
        LOCK(wallet->cs_wallet);
        wallet->AddKeyPubKey(key, key.GetPubKey());
    }
    const int NTX = 3;
    const int64_t NTIME = GetTime();
    for (int i = 0; i < NTX; ++i) {
        AddPaymentTx(*wallet, key, (i + 1) * COIN, NTIME);
    }
    std::unique_ptr<const PlatformStyle> platformStyle(PlatformStyle::instantiate("other"));
    auto node = interfaces::MakeNode();
    OptionsModel optionsModel(*node);
    AddWallet(wallet);
    WalletModel walletModel(std::move(node->getWallets().back()), *node, platformStyle.get(), &optionsModel);
    RemoveWallet(wallet);

    TransactionTableModel* ttm = walletModel.getTransactionTableModel();
    QCOMPARE(ttm->rowCount({}), NTX);
    const int total = ttm->rowCount({});

    TransactionFilterProxy proxy;
    proxy.setSourceModel(ttm);
    // Default [MIN_DATE, MAX_DATE] range shows everything.
    QCOMPARE(proxy.rowCount(), total);

    // Witness row: single-point inclusive range must contain it, ±1s must not.
    // Boundaries are derived from live DateRole values (no hardcoded epochs).
    // All fixture rows share one second, so the point range holds every row.
    const QModelIndex witness = ttm->index(0, TransactionTableModel::Date);
    const QDateTime dt = witness.data(TransactionTableModel::DateRole).toDateTime();
    QVERIFY(dt.isValid());
    proxy.setDateRange(dt, dt);
    QCOMPARE(proxy.rowCount(), total);
    QVERIFY(proxy.mapFromSource(ttm->index(0, 0)).isValid());
    proxy.setDateRange(dt.addSecs(1), TransactionFilterProxy::MAX_DATE);
    QCOMPARE(proxy.rowCount(), 0);
    QVERIFY(!proxy.mapFromSource(ttm->index(0, 0)).isValid());
    proxy.setDateRange(TransactionFilterProxy::MIN_DATE, dt.addSecs(-1));
    QCOMPARE(proxy.rowCount(), 0);
    QVERIFY(!proxy.mapFromSource(ttm->index(0, 0)).isValid());

    // Invalid bound exercises the legacy QDateTime compare path: the same
    // logical full range through the legacy path must show the same rows.
    proxy.setDateRange(QDateTime(), TransactionFilterProxy::MAX_DATE);
    QCOMPARE(proxy.rowCount(), total);

    // Full range again: everything visible.
    proxy.setDateRange(TransactionFilterProxy::MIN_DATE, TransactionFilterProxy::MAX_DATE);
    QCOMPARE(proxy.rowCount(), total);

    // Same assertions through the history-tab proxy (own int-compare path).
    BlocknetTransactionHistoryFilterProxy hproxy(&optionsModel);
    hproxy.setSourceModel(ttm);
    hproxy.setTypeFilter(BlocknetTransactionHistoryFilterProxy::ALL_TYPES);
    QCOMPARE(hproxy.rowCount(QModelIndex()), total);
    hproxy.setDateRange(dt, dt);
    QCOMPARE(hproxy.rowCount(QModelIndex()), total);
    QVERIFY(hproxy.mapFromSource(ttm->index(0, 0)).isValid());
    hproxy.setDateRange(dt.addSecs(1), BlocknetTransactionHistoryFilterProxy::MAX_DATE);
    QCOMPARE(hproxy.rowCount(QModelIndex()), 0);
    QVERIFY(!hproxy.mapFromSource(ttm->index(0, 0)).isValid());
    hproxy.setDateRange(BlocknetTransactionHistoryFilterProxy::MIN_DATE, dt.addSecs(-1));
    QCOMPARE(hproxy.rowCount(QModelIndex()), 0);
    QVERIFY(!hproxy.mapFromSource(ttm->index(0, 0)).isValid());
    hproxy.setDateRange(QDateTime(), BlocknetTransactionHistoryFilterProxy::MAX_DATE);
    QCOMPARE(hproxy.rowCount(QModelIndex()), total);
    hproxy.setDateRange(BlocknetTransactionHistoryFilterProxy::MIN_DATE, BlocknetTransactionHistoryFilterProxy::MAX_DATE);
    QCOMPARE(hproxy.rowCount(QModelIndex()), total);
}

void WalletTests::confirmationsDirtyTests()
{
#ifdef Q_OS_MAC
    if (QApplication::platformName() == "minimal") {
        QWARN("Skipping confirmationsDirtyTests on mac build with 'minimal' platform (QTBUG-49686).");
        return;
    }
#endif
    // Chain fixture (not the rescan): tryGetTxStatus reports the real chain
    // height per row, so the pushed height must equal the tip for rows to
    // converge. Rows come from hand-added payments: mined coinbases yield
    // zero rows here (Blocknet decompose skips PoW coinbase outputs).
    TestChain100Setup test;
    auto chain = interfaces::MakeChain();
    std::shared_ptr<CWallet> wallet = std::make_shared<CWallet>(*chain, WalletLocation(), WalletDatabase::CreateMock());
    bool firstRun;
    wallet->LoadWallet(firstRun);
    CKey key;
    key.MakeNewKey(true);
    {
        LOCK(wallet->cs_wallet);
        wallet->AddKeyPubKey(key, key.GetPubKey());
    }
    const int NTX = 3;
    const int64_t NTIME = GetTime();
    for (int i = 0; i < NTX; ++i) {
        AddPaymentTx(*wallet, key, (i + 1) * COIN, NTIME);
    }
    std::unique_ptr<const PlatformStyle> platformStyle(PlatformStyle::instantiate("other"));
    auto node = interfaces::MakeNode();
    OptionsModel optionsModel(*node);
    AddWallet(wallet);
    WalletModel walletModel(std::move(node->getWallets().back()), *node, platformStyle.get(), &optionsModel);
    RemoveWallet(wallet);

    TransactionTableModel* ttm = walletModel.getTransactionTableModel();
    QCOMPARE(ttm->rowCount({}), NTX);

    // First refresh at the tip height updates all stale rows (fresh records
    // start with cur_num_blocks == -1): exactly one span covering Status
    // through ToAddress over every row. That is the whole optimization:
    // Status + ToAddress, nothing else, no per-clean-row work.
    QSignalSpy spy(ttm, &QAbstractItemModel::dataChanged);
    const int tipHeight = chainActive.Height();
    ttm->updateConfirmations(tipHeight);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).toModelIndex().row(), 0);
    QCOMPARE(spy.at(0).at(0).toModelIndex().column(), TransactionTableModel::Status);
    QCOMPARE(spy.at(0).at(1).toModelIndex().row(), NTX - 1);
    QCOMPARE(spy.at(0).at(1).toModelIndex().column(), TransactionTableModel::ToAddress);
    spy.clear();
    // A repeated refresh at the same height must emit nothing. tryGetTxStatus
    // uses try-locks, so a concurrent WalletModel poll worker could (rarely)
    // leave a row dirty; retry a few times. An implementation that always
    // emits still fails: it never converges to zero.
    ttm->updateConfirmations(tipHeight);
    for (int i = 0; i < 2 && spy.count() > 0; ++i) {
        spy.clear();
        ttm->updateConfirmations(tipHeight);
    }
    QCOMPARE(spy.count(), 0);
    // Incremental add inserts one stale record at its hash-sorted position
    // while the rest are current: the next refresh must emit exactly one
    // single-row span over Status..ToAddress. Only single-row-ness and the
    // columns are asserted, not the position: any data() lookup would run
    // the index() fast-path and converge the row before the refresh runs.
    // Combined with the proven silence above, a single-row span must cover
    // exactly the new record.
    const uint256 hash4 = AddPaymentTx(*wallet, key, 4 * COIN, NTIME);
    ttm->updateTransaction(QString::fromStdString(hash4.ToString()), CT_NEW, true);
    QCOMPARE(ttm->rowCount({}), NTX + 1);
    ttm->updateConfirmations(tipHeight);
    QCOMPARE(spy.count(), 1);
    const int top = spy.at(0).at(0).toModelIndex().row();
    const int bottom = spy.at(0).at(1).toModelIndex().row();
    QCOMPARE(top, bottom);
    QCOMPARE(spy.at(0).at(0).toModelIndex().column(), TransactionTableModel::Status);
    QCOMPARE(spy.at(0).at(1).toModelIndex().row(), top);
    QCOMPARE(spy.at(0).at(1).toModelIndex().column(), TransactionTableModel::ToAddress);
}

void WalletTests::walletTests()
{
#ifdef Q_OS_MAC
    if (QApplication::platformName() == "minimal") {
        // Disable for mac on "minimal" platform to avoid crashes inside the Qt
        // framework when it tries to look up unimplemented cocoa functions,
        // and fails to handle returned nulls
        // (https://bugreports.qt.io/browse/QTBUG-49686).
        QWARN("Skipping WalletTests on mac build with 'minimal' platform set due to Qt bugs. To run AppTests, invoke "
              "with 'test_bitcoin-qt -platform cocoa' on mac, or else use a linux or windows build.");
        return;
    }
#endif
    TestGUI();
}
