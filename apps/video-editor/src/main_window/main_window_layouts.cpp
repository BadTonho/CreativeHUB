#include "main_window/main_window.h"

#include "logging/logger.h"
#include "ui/workspace/workspace_host.h"

#include <QAction>
#include <QApplication>
#include <QDialog>
#include <QDockWidget>
#include <QFont>
#include <QGridLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMenuBar>
#include <QMessageBox>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QScopeGuard>
#include <QScrollArea>
#include <QSettings>
#include <QStatusBar>
#include <QToolBar>
#include <QToolButton>
#include <QTimer>
#include <QUuid>
#include <QVBoxLayout>

#include <algorithm>
#include <functional>

namespace {

constexpr auto kLayoutsGroup = "workspace/saved_layouts";
constexpr int kSnapshotVersion = 1;
constexpr int kDockStateVersion = 9;

// The dock tree alone does not describe the two expansion actions or the
// remembered widths used when Preview owns an outer corner.
const QStringList& auxiliaryKeys() {
    static const QStringList keys{
        QStringLiteral("workspace/media_pool_width"),
        QStringLiteral("workspace/inspector_width"),
        QStringLiteral("workspace/media_pool_expanded"),
        QStringLiteral("workspace/inspector_expanded"),
        QStringLiteral("workspace/media_pool_expanded_width"),
        QStringLiteral("workspace/media_pool_normal_width"),
        QStringLiteral("workspace/dock_expansion_restore_layout_state"),
        QStringLiteral("workspace/media_pool_restore_layout_state"),
        QStringLiteral("workspace/preview_side_panel_widths_active"),
        QStringLiteral("workspace/preview_side_media_pool_width"),
        QStringLiteral("workspace/preview_side_toolbox_width"),
        QStringLiteral("workspace/preview_side_effects_width"),
        QStringLiteral("workspace/preview_side_saved_media_pool_width"),
        QStringLiteral("workspace/preview_side_saved_toolbox_width"),
        QStringLiteral("workspace/preview_side_saved_effects_width"),
        QStringLiteral("workspace/preview_normal_toolbox_width"),
        QStringLiteral("workspace/preview_normal_effects_width")};
    return keys;
}

void logLayoutError(const char* operation, const QString& cause) {
    logging::Logger::instance().log(
        logging::Level::Error, "workspace_layouts", operation,
        cause.toUtf8().toStdString(),
        {{"cause", cause.toUtf8().toStdString()}});
}

QList<QPair<QString, QVariantMap>> savedLayouts() {
    QSettings settings;
    settings.beginGroup(QLatin1String(kLayoutsGroup));
    QList<QPair<QString, QVariantMap>> layouts;
    for (const auto& id : settings.childKeys()) {
        const auto record = settings.value(id).toMap();
        if (!record.isEmpty()) layouts.append({id, record});
    }
    settings.endGroup();
    std::sort(layouts.begin(), layouts.end(), [](const auto& a, const auto& b) {
        return a.second.value(QStringLiteral("name")).toString().localeAwareCompare(
                   b.second.value(QStringLiteral("name")).toString()) < 0;
    });
    return layouts;
}

bool validUniqueName(const QString& name, const QString& exempt_id = {}) {
    if (name.isEmpty() || name.size() > 60) return false;
    for (const auto& [id, record] : savedLayouts()) {
        if (id != exempt_id &&
            record.value(QStringLiteral("name")).toString().compare(
                name, Qt::CaseInsensitive) == 0) return false;
    }
    return true;
}

bool writeRecord(const QString& id, const QVariantMap& record) {
    QSettings settings;
    const auto key = QStringLiteral("%1/%2").arg(QLatin1String(kLayoutsGroup), id);
    const auto previous = settings.value(key);
    const bool existed = settings.contains(key);
    settings.setValue(key, record);
    settings.sync();
    if (settings.status() == QSettings::NoError) return true;
    logLayoutError("save", QStringLiteral("Could not write saved panel layouts (settings error %1).")
                               .arg(static_cast<int>(settings.status())));
    if (existed) settings.setValue(key, previous);
    else settings.remove(key);
    settings.sync();
    return false;
}

QVariantMap tile(const QString& label, int x, int y, int w, int h,
                 bool floating = false) {
    return {{QStringLiteral("label"), label}, {QStringLiteral("x"), x},
            {QStringLiteral("y"), y}, {QStringLiteral("w"), w},
            {QStringLiteral("h"), h}, {QStringLiteral("floating"), floating}};
}

QVariantList builtInTiles(int preset) {
    const int timeline_top = preset == 2 ? 450 : preset == 1 ? 760 : 630;
    const int left_width = preset == 1 ? 170 : 220;
    const int right_x = preset == 1 ? 835 : 790;
    return {tile(QStringLiteral("Bins"), 0, 0, left_width, 260),
            tile(QStringLiteral("Media"), 0, 270, left_width, timeline_top - 270),
            tile(QStringLiteral("Preview"), left_width + 8, 0,
                 right_x - left_width - 16, timeline_top - 8),
            tile(QStringLiteral("Inspector"), right_x, 0, 1000 - right_x,
                 timeline_top - 8),
            tile(QStringLiteral("Timeline"), 0, timeline_top, 1000,
                 1000 - timeline_top)};
}

QIcon thumbnail(const QVariantList& tiles) {
    QPixmap pixmap(168, 100);
    pixmap.fill(QColor(QStringLiteral("#171a20")));
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setFont(QFont(QStringLiteral("Sans Serif"), 7));
    painter.setPen(QColor(QStringLiteral("#91a5bc")));
    painter.drawRect(pixmap.rect().adjusted(0, 0, -1, -1));
    const auto drawTile = [&painter, &pixmap](const QVariant& value) {
        const auto item = value.toMap();
        const bool floating = item.value(QStringLiteral("floating")).toBool();
        QRect rect(3 + item.value(QStringLiteral("x")).toInt() * 162 / 1000,
                   3 + item.value(QStringLiteral("y")).toInt() * 94 / 1000,
                   std::max(7, item.value(QStringLiteral("w")).toInt() * 162 / 1000),
                   std::max(7, item.value(QStringLiteral("h")).toInt() * 94 / 1000));
        rect = rect.intersected(pixmap.rect().adjusted(3, 3, -3, -3));
        if (rect.isEmpty()) return;
        painter.setBrush(QColor(floating ? QStringLiteral("#384451")
                                         : QStringLiteral("#29465f")));
        painter.setPen(QPen(QColor(QStringLiteral("#75b6e7")), 1,
                            floating ? Qt::DashLine : Qt::SolidLine));
        painter.drawRoundedRect(rect, 2, 2);
        if (rect.width() >= 28 && rect.height() >= 17) {
            painter.setPen(Qt::white);
            auto label = item.value(QStringLiteral("label")).toString();
            if (rect.width() < 55 && label.size() > 5) label = label.left(1);
            painter.drawText(rect.adjusted(2, 1, -2, -1), Qt::AlignCenter,
                             label);
        }
    };
    for (const auto& value : tiles) {
        if (!value.toMap().value(QStringLiteral("floating")).toBool())
            drawTile(value);
    }
    for (const auto& value : tiles) {
        if (value.toMap().value(QStringLiteral("floating")).toBool())
            drawTile(value);
    }
    return QIcon(pixmap);
}

} // namespace

QVariantMap MainWindow::captureWorkspaceLayout() const {
    QSettings settings;
    QVariantMap auxiliary;
    for (const auto& key : auxiliaryKeys()) {
        if (settings.contains(key)) auxiliary.insert(key, settings.value(key));
    }
    auxiliary.insert(QStringLiteral("workspace/media_pool_expanded"), media_pool_expanded_);
    auxiliary.insert(QStringLiteral("workspace/inspector_expanded"), inspector_expanded_);
    auxiliary.insert(QStringLiteral("workspace/preview_side_panel_widths_active"),
                     preview_side_panel_widths_active_);
    if (!workspace_expansion_restore_layout_state_.isEmpty()) {
        auxiliary.insert(QStringLiteral("workspace/dock_expansion_restore_layout_state"),
                         workspace_expansion_restore_layout_state_);
    }
    QVariantList tiles;
    QVariantMap floating_geometries;
    const QList<QPair<QDockWidget*, QString>> docks{
        {bins_dock_, QStringLiteral("Bins")},
        {media_dock_, QStringLiteral("Media")},
        {toolbox_dock_, QStringLiteral("Toolbox")},
        {favorites_dock_, QStringLiteral("Favorites")},
        {effects_dock_, QStringLiteral("Effects")},
        {preview_dock_, QStringLiteral("Preview")},
        {inspector_dock_, QStringLiteral("Inspector")},
        {timeline_dock_, QStringLiteral("Timeline")}};
    int floating_index = 0;
    const int top = menuBar() != nullptr ? menuBar()->height() + 30 : 0;
    const int bottom = statusBar() != nullptr ? statusBar()->geometry().top() : height();
    const int workspace_height = std::max(1, bottom - top);
    for (const auto& [dock, label] : docks) {
        if (dock == nullptr || dock->isHidden()) continue;
        if (dock->isFloating()) {
            floating_geometries.insert(dock->objectName(), dock->saveGeometry());
            tiles.append(tile(label, 680 + (floating_index % 3) * 35,
                              80 + (floating_index % 3) * 35, 230, 260, true));
            ++floating_index;
            continue;
        }
        const auto origin = dock->mapTo(this, QPoint(0, 0));
        tiles.append(tile(label,
                          std::clamp(origin.x() * 1000 / std::max(1, width()), 0, 1000),
                          std::clamp((origin.y() - top) * 1000 / workspace_height, 0, 1000),
                          std::clamp(dock->width() * 1000 / std::max(1, width()), 0, 1000),
                          std::clamp(dock->height() * 1000 / workspace_height, 0, 1000)));
    }
    return {{QStringLiteral("version"), kSnapshotVersion},
            {QStringLiteral("dock_state"), saveState(kDockStateVersion)},
            {QStringLiteral("auxiliary"), auxiliary},
            {QStringLiteral("floating_geometry"), floating_geometries},
            {QStringLiteral("tiles"), tiles}};
}

bool MainWindow::applyWorkspaceLayout(const QVariantMap& snapshot) {
    const auto state = snapshot.value(QStringLiteral("dock_state")).toByteArray();
    if (snapshot.value(QStringLiteral("version")).toInt() != kSnapshotVersion ||
        state.isEmpty()) {
        logLayoutError("restore", QStringLiteral("Saved panel layout is missing or has an unsupported version."));
        return false;
    }
    const auto previous = captureWorkspaceLayout();
    const auto previous_options = dockOptions();
    const bool previous_restoring = restoring_default_dock_layout_;
    restoring_default_dock_layout_ = true;
    setDockOptions(previous_options & ~QMainWindow::AnimatedDocks);
    const auto guard = qScopeGuard([this, previous_options, previous_restoring] {
        setDockOptions(previous_options);
        restoring_default_dock_layout_ = previous_restoring;
    });
    for (auto* timer : {media_pool_width_save_timer_,
                        preview_side_panel_width_save_timer_, inspector_width_save_timer_}) {
        if (timer != nullptr) timer->stop();
    }
    const auto restore = [this](const QVariantMap& value) {
        QSettings settings;
        const auto auxiliary = value.value(QStringLiteral("auxiliary")).toMap();
        for (const auto& key : auxiliaryKeys()) {
            if (auxiliary.contains(key)) settings.setValue(key, auxiliary.value(key));
            else settings.remove(key);
        }
        media_pool_expanded_ = auxiliary.value(
            QStringLiteral("workspace/media_pool_expanded")).toBool();
        inspector_expanded_ = auxiliary.value(
            QStringLiteral("workspace/inspector_expanded")).toBool();
        preview_side_panel_widths_active_ = auxiliary.value(
            QStringLiteral("workspace/preview_side_panel_widths_active")).toBool();
        workspace_expansion_restore_layout_state_ = auxiliary.value(
            QStringLiteral("workspace/dock_expansion_restore_layout_state")).toByteArray();
        preview_side_layout_restore_pending_ = false;
        preview_side_width_restore_pending_ = false;
        preview_side_dock_full_height_ = false;
        setCorner(Qt::BottomLeftCorner, Qt::BottomDockWidgetArea);
        setCorner(Qt::BottomRightCorner, Qt::BottomDockWidgetArea);
        const bool restored = restoreState(
            value.value(QStringLiteral("dock_state")).toByteArray(),
            kDockStateVersion);
        if (restored) {
            const auto geometries = value.value(
                QStringLiteral("floating_geometry")).toMap();
            for (auto* dock : {bins_dock_, media_dock_, toolbox_dock_,
                               favorites_dock_, effects_dock_, preview_dock_,
                               inspector_dock_, timeline_dock_}) {
                if (dock == nullptr || !dock->isFloating()) continue;
                const auto geometry = geometries.value(dock->objectName()).toByteArray();
                if (!geometry.isEmpty()) dock->restoreGeometry(geometry);
            }
        }
        updateMediaPoolExpandButton();
        updateInspectorExpandButton();
        updateMediaPoolActionState();
        updateEffectsActionState();
        if (workspace_host_ != nullptr) {
            workspace_host_->refreshCentralWorkspaceVisibility();
        }
        layout()->activate();
        return restored;
    };
    if (!restore(snapshot)) {
        if (!restore(previous)) {
            logLayoutError("rollback", QStringLiteral("Qt could not restore the previous panel layout after a failed load."));
        }
        logLayoutError("restore", QStringLiteral("Qt could not restore the saved panel layout."));
        return false;
    }
    saveWorkspaceLayout();
    QTimer::singleShot(0, this, [this] { schedulePreviewDockLayoutUpdate(); });
    return true;
}

bool MainWindow::saveNamedWorkspaceLayout(const QString& name, const QString& id) {
    if (workspace_host_ == nullptr ||
        workspace_host_->currentPage() == ui::WorkspacePageId::Render) return false;
    const auto clean_name = name.trimmed();
    if (!validUniqueName(clean_name, id)) return false;
    if (!id.isEmpty() && !QSettings().contains(
            QStringLiteral("%1/%2").arg(QLatin1String(kLayoutsGroup), id))) return false;
    persistPreviewSidePanelWidths();
    persistMediaPoolWidth();
    persistInspectorWidth();
    auto record = captureWorkspaceLayout();
    record.insert(QStringLiteral("name"), clean_name);
    const auto key = id.isEmpty()
        ? QUuid::createUuid().toString(QUuid::WithoutBraces) : id;
    return writeRecord(key, record);
}

bool MainWindow::renameNamedWorkspaceLayout(const QString& id, const QString& name) {
    QSettings settings;
    const auto key = QStringLiteral("%1/%2").arg(QLatin1String(kLayoutsGroup), id);
    if (!settings.contains(key)) return false;
    const auto clean_name = name.trimmed();
    if (!validUniqueName(clean_name, id)) return false;
    auto record = settings.value(key).toMap();
    record.insert(QStringLiteral("name"), clean_name);
    return writeRecord(id, record);
}

bool MainWindow::removeNamedWorkspaceLayout(const QString& id) {
    QSettings settings;
    const auto key = QStringLiteral("%1/%2").arg(QLatin1String(kLayoutsGroup), id);
    if (!settings.contains(key)) return false;
    const auto previous = settings.value(key);
    settings.remove(key);
    settings.sync();
    if (settings.status() == QSettings::NoError) return true;
    logLayoutError("delete", QStringLiteral("Could not delete saved panel layout (settings error %1).")
                              .arg(static_cast<int>(settings.status())));
    settings.setValue(key, previous);
    settings.sync();
    return false;
}

bool MainWindow::applyNamedWorkspaceLayout(const QString& id) {
    const auto key = QStringLiteral("%1/%2").arg(QLatin1String(kLayoutsGroup), id);
    QSettings settings;
    if (!settings.contains(key)) return false;
    return applyWorkspaceLayout(settings.value(key).toMap());
}

void MainWindow::applyBuiltInWorkspaceLayout(int preset) {
    if (preset < 0 || preset > 2 || workspace_host_ == nullptr ||
        workspace_host_->currentPage() == ui::WorkspacePageId::Render) return;
    restoreDefaultLayout();
    if (preset == 1) {
        resizeDocks({bins_dock_, preview_dock_, inspector_dock_},
                    {std::max(170, width() * 17 / 100),
                     std::max(360, width() * 66 / 100),
                     std::max(170, width() * 17 / 100)}, Qt::Horizontal);
        resizeDocks({preview_dock_, timeline_dock_},
                    {std::max(300, height() * 74 / 100),
                     std::max(160, height() * 26 / 100)}, Qt::Vertical);
    } else if (preset == 2) {
        resizeDocks({bins_dock_, preview_dock_, inspector_dock_},
                    {std::max(180, width() * 20 / 100),
                     std::max(320, width() * 60 / 100),
                     std::max(180, width() * 20 / 100)}, Qt::Horizontal);
        resizeDocks({preview_dock_, timeline_dock_},
                    {std::max(230, height() * 43 / 100),
                     std::max(300, height() * 57 / 100)}, Qt::Vertical);
    }
    layout()->activate();
    persistMediaPoolWidth();
    persistInspectorWidth();
    saveWorkspaceLayout();
}

void MainWindow::showWorkspaceLayouts() {
    if (workspace_host_ == nullptr ||
        workspace_host_->currentPage() == ui::WorkspacePageId::Render) return;
    QDialog dialog(this);
    dialog.setObjectName(QStringLiteral("workspaceLayoutsGallery"));
    dialog.setWindowTitle(QCoreApplication::translate("MainWindow", "Panel Layouts"));
    dialog.resize(720, 500);
    dialog.setStyleSheet(
        "QDialog, QScrollArea, QScrollArea > QWidget > QWidget {"
        " background: #1e1e1e; color: #f2f2f2; }"
        "QLabel { color: #f2f2f2; font-weight: 600; }"
        "QPushButton, QToolButton { background: #303030; color: #f2f2f2;"
        " border: 1px solid #555; border-radius: 4px; padding: 5px; }"
        "QPushButton:hover, QToolButton:hover { background: #3b4d5d;"
        " border-color: #75b6e7; }"
        "QPushButton:pressed, QToolButton:pressed { background: #245278; }");
    auto* root = new QVBoxLayout(&dialog);
    auto* save_button = new QPushButton(
        QCoreApplication::translate("MainWindow", "Save Current Layout..."), &dialog);
    save_button->setObjectName(QStringLiteral("saveCurrentLayoutButton"));
    root->addWidget(save_button, 0, Qt::AlignLeft);
    auto* scroll = new QScrollArea(&dialog);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    root->addWidget(scroll);
    std::function<void()> rebuild;
    const auto showFailure = [this, &dialog] {
        QMessageBox::warning(&dialog,
            QCoreApplication::translate("MainWindow", "Panel Layouts"),
            QCoreApplication::translate("MainWindow", "The layout could not be saved or restored. Check the error log."));
    };
    const auto requestName = [&dialog](const QString& title, const QString& existing) {
        bool accepted = false;
        const auto name = QInputDialog::getText(
            &dialog, title,
            QCoreApplication::translate("MainWindow", "Layout name:"),
            QLineEdit::Normal, existing, &accepted).trimmed();
        return accepted ? name : QString();
    };
    connect(save_button, &QPushButton::clicked, &dialog, [&] {
        const auto name = requestName(
            QCoreApplication::translate("MainWindow", "Save Layout"), {});
        if (name.isEmpty()) return;
        if (!saveNamedWorkspaceLayout(name)) {
            QMessageBox::warning(&dialog, dialog.windowTitle(),
                QCoreApplication::translate("MainWindow", "Choose a unique name of 1 to 60 characters, or check the error log."));
            return;
        }
        rebuild();
    });
    rebuild = [&] {
        delete scroll->takeWidget();
        auto* content = new QWidget(scroll);
        auto* grid = new QGridLayout(content);
        grid->setSpacing(10);
        auto* built_in_label = new QLabel(
            QCoreApplication::translate("MainWindow", "Ready layouts"), content);
        grid->addWidget(built_in_label, 0, 0, 1, 3);
        const QStringList built_in_names{
            QCoreApplication::translate("MainWindow", "Default"),
            QCoreApplication::translate("MainWindow", "Preview Focus"),
            QCoreApplication::translate("MainWindow", "Timeline Focus")};
        for (int index = 0; index < built_in_names.size(); ++index) {
            auto* button = new QToolButton(content);
            button->setObjectName(QStringLiteral("builtInLayout%1").arg(index));
            button->setText(built_in_names[index]);
            button->setIcon(thumbnail(builtInTiles(index)));
            button->setIconSize(QSize(168, 100));
            button->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
            button->setMinimumWidth(190);
            grid->addWidget(button, 1, index);
            connect(button, &QToolButton::clicked, &dialog, [this, &dialog, index] {
                applyBuiltInWorkspaceLayout(index);
                dialog.accept();
            });
        }
        auto* saved_label = new QLabel(
            QCoreApplication::translate("MainWindow", "Your layouts"), content);
        grid->addWidget(saved_label, 2, 0, 1, 3);
        int row = 3;
        int column = 0;
        for (const auto& [id, record] : savedLayouts()) {
            auto* card = new QWidget(content);
            auto* card_layout = new QVBoxLayout(card);
            card_layout->setContentsMargins(2, 2, 2, 2);
            auto* apply = new QToolButton(card);
            apply->setObjectName(QStringLiteral("savedLayout_%1").arg(id));
            apply->setText(record.value(QStringLiteral("name")).toString());
            apply->setIcon(thumbnail(record.value(QStringLiteral("tiles")).toList()));
            apply->setIconSize(QSize(168, 100));
            apply->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
            apply->setMinimumWidth(190);
            card_layout->addWidget(apply);
            connect(apply, &QToolButton::clicked, &dialog, [this, &dialog, id, showFailure] {
                if (applyNamedWorkspaceLayout(id)) dialog.accept();
                else showFailure();
            });
            auto* actions = new QHBoxLayout();
            const auto addAction = [card, actions](const QString& text,
                                                   const QString& object_name) {
                auto* button = new QPushButton(text, card);
                button->setObjectName(object_name);
                actions->addWidget(button);
                return button;
            };
            auto* update = addAction(QCoreApplication::translate("MainWindow", "Update"),
                                     QStringLiteral("updateLayout_%1").arg(id));
            auto* rename = addAction(QCoreApplication::translate("MainWindow", "Rename"),
                                     QStringLiteral("renameLayout_%1").arg(id));
            auto* remove = addAction(QCoreApplication::translate("MainWindow", "Delete"),
                                     QStringLiteral("deleteLayout_%1").arg(id));
            card_layout->addLayout(actions);
            connect(update, &QPushButton::clicked, &dialog, [&, id] {
                if (QMessageBox::question(&dialog, dialog.windowTitle(),
                    QCoreApplication::translate("MainWindow", "Replace this saved layout with the current panel arrangement?")) != QMessageBox::Yes) return;
                const auto name = QSettings().value(
                    QStringLiteral("%1/%2").arg(QLatin1String(kLayoutsGroup), id)).toMap()
                    .value(QStringLiteral("name")).toString();
                if (!saveNamedWorkspaceLayout(name, id)) showFailure();
                else QTimer::singleShot(0, &dialog, rebuild);
            });
            connect(rename, &QPushButton::clicked, &dialog, [&, id] {
                const auto current_name = QSettings().value(
                    QStringLiteral("%1/%2").arg(QLatin1String(kLayoutsGroup), id)).toMap()
                    .value(QStringLiteral("name")).toString();
                const auto name = requestName(
                    QCoreApplication::translate("MainWindow", "Rename Layout"), current_name);
                if (name.isEmpty()) return;
                if (!renameNamedWorkspaceLayout(id, name)) {
                    QMessageBox::warning(&dialog, dialog.windowTitle(),
                        QCoreApplication::translate("MainWindow", "Choose a unique name of 1 to 60 characters, or check the error log."));
                } else QTimer::singleShot(0, &dialog, rebuild);
            });
            connect(remove, &QPushButton::clicked, &dialog, [&, id] {
                if (QMessageBox::question(&dialog, dialog.windowTitle(),
                    QCoreApplication::translate("MainWindow", "Delete this saved layout?")) != QMessageBox::Yes) return;
                if (!removeNamedWorkspaceLayout(id)) showFailure();
                else QTimer::singleShot(0, &dialog, rebuild);
            });
            grid->addWidget(card, row, column);
            if (++column == 3) { column = 0; ++row; }
        }
        grid->setRowStretch(row + 1, 1);
        scroll->setWidget(content);
    };
    rebuild();
    dialog.exec();
}
