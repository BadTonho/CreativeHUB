#pragma once
#include <creative_suite/effects/adjustment.h>
#include <QWidget>
#include <QComboBox>
#include <QCheckBox>
#include <QSpinBox>
#include <QSlider>
#include <QPushButton>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QCoreApplication>
#include <QEvent>
#include <QTimer>
#include <array>
#include <functional>
namespace image_editor {
class ColorBalanceEditor final : public QWidget {
public:
    using Parameters = creative_suite::effects::ColorBalanceParameters;
    std::function<void()> started, finished;
    std::function<void(const Parameters&)> changed;
    explicit ColorBalanceEditor(QWidget* parent) : QWidget(parent) {
        setObjectName("colorBalanceEditor");
        auto* layout = new QVBoxLayout(this); layout->setContentsMargins(0,0,0,0);
        range_ = new QComboBox(this); range_->setObjectName("colorBalanceRange");
        for (const char* label : {QT_TRANSLATE_NOOP("ImageEditorUI", "Shadows"), QT_TRANSLATE_NOOP("ImageEditorUI", "Midtones"), QT_TRANSLATE_NOOP("ImageEditorUI", "Highlights")})
            range_->addItem(QCoreApplication::translate("ImageEditorUI",label));
        range_->setCurrentIndex(1); layout->addWidget(range_);
        connect(range_, &QComboBox::currentIndexChanged, this, [this] { if (!syncing_) { finish(); sync(); } });
        const char* labels[]{QT_TRANSLATE_NOOP("ImageEditorUI", "Cyan / Red"), QT_TRANSLATE_NOOP("ImageEditorUI", "Magenta / Green"), QT_TRANSLATE_NOOP("ImageEditorUI", "Yellow / Blue")};
        for (int i=0;i<3;++i) {
            auto* row=new QHBoxLayout; auto label=QCoreApplication::translate("ImageEditorUI",labels[i]);
            row->addWidget(new QLabel(label,this)); sliders_[i]=new QSlider(Qt::Horizontal,this); spins_[i]=new QSpinBox(this);
            sliders_[i]->setRange(-100,100); spins_[i]->setRange(-100,100);
            sliders_[i]->setObjectName(QStringLiteral("colorBalanceSlider%1").arg(i));
            spins_[i]->setObjectName(QStringLiteral("colorBalanceSpin%1").arg(i));
            sliders_[i]->setAccessibleName(label); spins_[i]->setAccessibleName(label);
            sliders_[i]->installEventFilter(this); spins_[i]->installEventFilter(this);
            row->addWidget(sliders_[i]); row->addWidget(spins_[i]); layout->addLayout(row);
            connect(sliders_[i],&QSlider::sliderPressed,this,[this]{finish();begin();});
            connect(sliders_[i],&QSlider::valueChanged,this,[this,i](int v){edit(i,v);});
            connect(spins_[i],&QSpinBox::valueChanged,this,[this,i](int v){edit(i,v);});
            connect(sliders_[i],&QSlider::sliderReleased,this,[this]{finish();});
            connect(spins_[i],&QSpinBox::editingFinished,this,[this]{finish();});
        }
        preserve_=new QCheckBox(QCoreApplication::translate("ImageEditorUI","Preserve Lightness"),this);
        preserve_->setObjectName("colorBalancePreserveLightness"); layout->addWidget(preserve_);
        connect(preserve_,&QCheckBox::toggled,this,[this](bool value){if(syncing_||value_.preserve_lightness==value)return;finish();begin();value_.preserve_lightness=value;sync();if(changed)changed(value_);finish();});
        reset_=new QPushButton(QCoreApplication::translate("ImageEditorUI","Reset"),this);
        reset_->setObjectName("resetColorBalanceButton"); layout->addWidget(reset_);
        connect(reset_,&QPushButton::clicked,this,[this]{finish();auto next=value_;next.ranges[range_->currentIndex()]={};if(next==value_)return;begin();value_=next;sync();if(changed)changed(value_);finish();});
        sync();
    }
    void setValue(const QString& id,const Parameters& value) { if(id_!=id){finish();range_->setCurrentIndex(1);} id_=id;value_=value;sync(); }
    void finish(){if(active_){active_=false;if(finished)finished();}}
protected:
    bool eventFilter(QObject* watched,QEvent* event) override {
        if(event->type()==QEvent::FocusOut)finish();
        for(auto* slider:sliders_)if(watched==slider){if(event->type()==QEvent::KeyRelease)finish();if(event->type()==QEvent::Wheel)QTimer::singleShot(0,this,[this]{finish();});}
        return QWidget::eventFilter(watched,event);
    }
private:
    void begin(){if(!active_){active_=true;if(started)started();}}
    void edit(int i,int v){if(syncing_||value_.ranges[range_->currentIndex()][i]==v)return;begin();value_.ranges[range_->currentIndex()][i]=v;sync();if(changed)changed(value_);}
    void sync(){syncing_=true;const auto& r=value_.ranges[range_->currentIndex()];for(int i=0;i<3;++i){sliders_[i]->setValue(r[i]);spins_[i]->setValue(r[i]);}preserve_->setChecked(value_.preserve_lightness);reset_->setEnabled(r!=std::array<int,3>{});syncing_=false;}
    QString id_; Parameters value_; std::array<QSlider*,3> sliders_{}; std::array<QSpinBox*,3> spins_{};
    QComboBox* range_{}; QCheckBox* preserve_{}; QPushButton* reset_{}; bool syncing_=false,active_=false;
};
}
