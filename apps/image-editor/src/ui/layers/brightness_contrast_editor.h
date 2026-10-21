#pragma once
#include <creative_suite/effects/adjustment.h>
#include <QWidget>
#include <QSlider>
#include <QSpinBox>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QCoreApplication>
#include <QEvent>
#include <QTimer>
#include <functional>
namespace image_editor {
class BrightnessContrastEditor final:public QWidget {
public:
    using Parameters=creative_suite::effects::BrightnessContrastParameters;
    std::function<void()> started,finished;
    std::function<void(const Parameters&)> changed;
    explicit BrightnessContrastEditor(QWidget* parent):QWidget(parent){
        setObjectName("brightnessContrastEditor");auto* layout=new QVBoxLayout(this);layout->setContentsMargins(0,0,0,0);
        const char* names[]{QT_TRANSLATE_NOOP("ImageEditorUI","Brightness"),QT_TRANSLATE_NOOP("ImageEditorUI","Contrast")};
        for(int i=0;i<2;++i){auto* row=new QHBoxLayout;row->addWidget(new QLabel(QCoreApplication::translate("ImageEditorUI",names[i]),this));
            sliders_[i]=new QSlider(Qt::Horizontal,this);sliders_[i]->setRange(-100,100);sliders_[i]->setObjectName(i?"contrastSlider":"brightnessSlider");
            spins_[i]=new QSpinBox(this);spins_[i]->setRange(-100,100);spins_[i]->setObjectName(i?"contrastSpin":"brightnessSpin");
            sliders_[i]->setAccessibleName(QCoreApplication::translate("ImageEditorUI",names[i]));spins_[i]->setAccessibleName(sliders_[i]->accessibleName());
            sliders_[i]->installEventFilter(this);spins_[i]->installEventFilter(this);
            row->addWidget(sliders_[i]);row->addWidget(spins_[i]);layout->addLayout(row);
            connect(sliders_[i],&QSlider::sliderPressed,this,[this]{finish();begin();});
            connect(sliders_[i],&QSlider::valueChanged,this,[this,i](int value){edit(i,value);});
            connect(spins_[i],&QSpinBox::valueChanged,this,[this,i](int value){edit(i,value);});
            connect(sliders_[i],&QSlider::sliderReleased,this,[this]{finish();});
            connect(spins_[i],&QSpinBox::editingFinished,this,[this]{finish();});
        }
        reset_=new QPushButton(QCoreApplication::translate("ImageEditorUI","Reset"),this);reset_->setObjectName("resetBrightnessContrastButton");layout->addWidget(reset_);
        connect(reset_,&QPushButton::clicked,this,[this]{finish();if(value_==Parameters{})return;begin();value_={};sync();if(changed)changed(value_);finish();});
    }
    void setValue(const QString& id,const Parameters& value){if(id_!=id)finish();id_=id;value_=value;sync();}
    void finish(){if(!active_)return;active_=false;if(finished)finished();}
protected:
    bool eventFilter(QObject* watched,QEvent* event) override {
        if(event->type()==QEvent::FocusOut)finish();
        if(watched==sliders_[0] || watched==sliders_[1]){
            if(event->type()==QEvent::KeyRelease)finish();
            if(event->type()==QEvent::Wheel)QTimer::singleShot(0,this,[this]{finish();});
        }
        return QWidget::eventFilter(watched,event);
    }
private:
    void begin(){if(!active_){active_=true;if(started)started();}}
    void edit(int field,int value){if(syncing_)return;auto next=value_;(field?next.contrast:next.brightness)=value;if(next==value_)return;begin();value_=next;sync();if(changed)changed(value_);}
    void sync(){syncing_=true;sliders_[0]->setValue(value_.brightness);spins_[0]->setValue(value_.brightness);sliders_[1]->setValue(value_.contrast);spins_[1]->setValue(value_.contrast);reset_->setEnabled(value_!=Parameters{});syncing_=false;}
    QString id_;Parameters value_;QSlider* sliders_[2]{};QSpinBox* spins_[2]{};QPushButton* reset_{};bool syncing_=false,active_=false;
};
} // namespace image_editor
