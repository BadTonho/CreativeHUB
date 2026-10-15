#include "ui/translation_helpers.h"
#include "mask_inspector_widget.h"
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QPushButton>
#include <QLabel>
#include <QListWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QSignalBlocker>
#include <QStringList>
#include <algorithm>
namespace motion::ui {
namespace c=creative_suite::composition;
namespace {const QStringList names{MotionUiTranslation::tr("Center X"),MotionUiTranslation::tr("Center Y"),MotionUiTranslation::tr("Width"),MotionUiTranslation::tr("Height"),MotionUiTranslation::tr("Rotation"),MotionUiTranslation::tr("Soft Edge")};}
MaskInspectorWidget::MaskInspectorWidget(QWidget* parent):QWidget(parent) {
    setObjectName("motion-mask-inspector");
    setStyleSheet(QStringLiteral(
        "QPushButton:checked { color: #171a20; background-color: #e8b94f; "
        "border: 1px solid #e8b94f; border-radius: 3px; }"));
    auto* layout=new QVBoxLayout(this);auto* form=new QFormLayout;
    targets_=new QComboBox(this);targets_->setObjectName("motion-mask-target");form->addRow(MotionUiTranslation::tr("Apply to"),targets_);
    masks_=new QComboBox(this);masks_->setObjectName("motion-mask-selection");form->addRow(MotionUiTranslation::tr("Mask"),masks_);
    auto* create=new QHBoxLayout;
    for(int i=0;i<2;++i){auto* button=new QPushButton(i==0?MotionUiTranslation::tr("New Rectangle"):MotionUiTranslation::tr("New Ellipse"),this);button->setObjectName(i==0?"motion-mask-new-rectangle":"motion-mask-new-ellipse");create->addWidget(button);
        connect(button,&QPushButton::clicked,this,[this,i]{if(edit_)edit_([=,this](auto& doc){return doc.createMask(layer_,target_,i==0?c::ShapeMaskKind::Rectangle:c::ShapeMaskKind::Ellipse)!=0;});});}
    form->addRow(create);
    for(std::size_t i=0;i<6;++i) {
        auto* row=new QHBoxLayout;auto* field=new QDoubleSpinBox(this);fields_[i]=field;
        field->setObjectName(QString("motion-mask-value-%1").arg(i));field->setDecimals(3);
        field->setRange(i<2?-200:i==4?-3600:0,i<2?300:i<4?400:i==4?3600:100);field->setSuffix(i==4?" deg":" %");row->addWidget(field);
        if(i<5){auto* key=new QPushButton("K",this);keys_[i]=key;key->setObjectName(QString("motion-mask-key-%1").arg(i));key->setCheckable(true);key->setFixedWidth(28);row->addWidget(key);connect(key,&QPushButton::clicked,this,[this,i]{toggleKey(i);});}
        form->addRow(names[i],row);
    }
    invert_=new QCheckBox(MotionUiTranslation::tr("Invert"),this);invert_->setObjectName("motion-mask-invert");form->addRow(invert_);
    auto* apply=new QPushButton(MotionUiTranslation::tr("Apply Settings"),this);apply->setObjectName("motion-mask-apply");form->addRow(apply);
    uses_=new QLabel(this);uses_->setWordWrap(true);uses_->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Preferred);form->addRow(uses_);
    view_button_=new QPushButton(MotionUiTranslation::tr("View Mask"),this);view_button_->setCheckable(true);view_button_->setObjectName("motion-mask-view");form->addRow(view_button_);
    layout->addLayout(form);
    properties_=new QComboBox(this);for(int i=0;i<5;++i)properties_->addItem(names[i]);layout->addWidget(properties_);
    key_list_=new QListWidget(this);key_list_->setObjectName("motion-mask-keyframes");key_list_->setMaximumHeight(160);layout->addWidget(key_list_);layout->addStretch();
    connect(apply,&QPushButton::clicked,this,[this]{this->apply();});
    connect(targets_,&QComboBox::currentIndexChanged,this,[this]{if(syncing_)return;clearView();target_=targets_->currentData().toInt();refresh();});
    connect(masks_,&QComboBox::currentIndexChanged,this,[this]{if(syncing_||!edit_)return;clearView();const auto id=masks_->currentData().toULongLong();edit_([=,this](auto& doc){return doc.associateMask(layer_,target_,id);});});
    connect(properties_,&QComboBox::currentIndexChanged,this,[this]{if(!syncing_)refresh();});
    connect(key_list_,&QListWidget::itemClicked,this,[this](auto* item){if(curve_ && selectedMask())curve_(selectedMask(),properties_->currentIndex(),item->data(Qt::UserRole).toLongLong());});
    connect(view_button_,&QPushButton::toggled,this,[this](bool enabled){if(!syncing_ && view_)view_(layer_,target_,enabled);});
    setEnabled(false);
}
void MaskInspectorWidget::setHandlers(EditHandler edit,ViewHandler view,CurveHandler curve){edit_=std::move(edit);view_=std::move(view);curve_=std::move(curve);}
model::MaskId MaskInspectorWidget::selectedMask() const{return document_?document_->maskForTarget(layer_,target_):0;}
void MaskInspectorWidget::clearView(){if(view_button_->isChecked()){QSignalBlocker block(view_button_);view_button_->setChecked(false);if(view_)view_(layer_,target_,false);}}
void MaskInspectorWidget::sync(const model::CompositionDocument* document,model::LayerId layer,std::int64_t frame){
    if(document_!=document || layer_!=layer){clearView();target_=-2;}
    document_=document;layer_=layer;frame_=frame;refresh();
}
void MaskInspectorWidget::refresh(){
    syncing_=true;
    const model::CompositionLayer* layer=nullptr;
    if(document_)for(const auto& l:document_->layers())if(l.id==layer_){layer=&l;break;}
    setEnabled(layer!=nullptr);targets_->clear();masks_->clear();key_list_->clear();
    targets_->addItem(MotionUiTranslation::tr("Content"),-2);targets_->addItem(MotionUiTranslation::tr("Transformation"),-1);
    if(layer)for(std::size_t i=0;i<layer->effects.size();++i)targets_->addItem(QString(MotionUiTranslation::tr("Effect %1: %2")).arg(i+1).arg(std::holds_alternative<model::GaussianBlurEffect>(layer->effects[i])?MotionUiTranslation::tr("Gaussian Blur"):MotionUiTranslation::tr("Color Adjustment")),int(i));
    if(targets_->findData(target_)<0)target_=-2;
    targets_->setCurrentIndex(targets_->findData(target_));
    masks_->addItem(MotionUiTranslation::tr("None"),QVariant::fromValue<qulonglong>(0));
    if(document_)for(const auto& m:document_->masks())masks_->addItem(QString("%1 #%2").arg(m.kind==c::ShapeMaskKind::Rectangle?MotionUiTranslation::tr("Rectangle Mask"):MotionUiTranslation::tr("Ellipse Mask")).arg(m.id),QVariant::fromValue<qulonglong>(m.id));
    const auto id=selectedMask();masks_->setCurrentIndex(masks_->findData(QVariant::fromValue<qulonglong>(id)));
    const auto* mask=document_?document_->findMask(id):nullptr;
    const auto values=mask?c::evaluateShapeMask(mask->parameters,mask->keyframes,frame_):c::ShapeMaskParameters{};
    for(std::size_t i=0;i<6;++i){fields_[i]->setEnabled(mask);fields_[i]->setValue(i==5?values.soft_edge*100:c::shapeMaskValue(values,i)*(i==4?1:100));shown_values_[i]=fields_[i]->value();if(i<5){keys_[i]->setEnabled(mask);keys_[i]->setChecked(mask && std::any_of(mask->keyframes[i].begin(),mask->keyframes[i].end(),[this](const auto& k){return k.frame==frame_;}));}}
    invert_->setEnabled(mask);invert_->setChecked(values.invert);view_button_->setEnabled(mask);
    uses_->setText(mask?MotionUiTranslation::tr("Used by %1 destination(s).\nKeys use composition frames.").arg(document_->maskUseCount(id)):MotionUiTranslation::tr("Choose or create a mask."));
    if(mask)for(const auto& k:mask->keyframes[properties_->currentIndex()]){auto* item=new QListWidgetItem(QString(MotionUiTranslation::tr("Frame %1: %2")).arg(k.frame).arg(k.value),key_list_);item->setData(Qt::UserRole,QVariant::fromValue<qlonglong>(k.frame));}
    syncing_=false;if(!mask)clearView();
}
void MaskInspectorWidget::apply(){
    if(!edit_||!selectedMask())return;
    const auto id=selectedMask();
    auto values=c::evaluateShapeMask(document_->findMask(id)->parameters,document_->findMask(id)->keyframes,frame_);
    std::array<bool,5> changed_geometry{};
    for(std::size_t i=0;i<5;++i) {
        changed_geometry[i]=fields_[i]->value()!=shown_values_[i];
        if(changed_geometry[i])c::setShapeMaskValue(values,i,fields_[i]->value()/(i==4?1:100));
    }
    if(fields_[5]->value()!=shown_values_[5])values.soft_edge=fields_[5]->value()/100;
    values.invert=invert_->isChecked();
    edit_([=,this](auto& doc){
        const auto* old=doc.findMask(id);if(!old)return false;
        const auto original=*old;auto base=values;bool changed=false;
        for(std::size_t i=0;i<5;++i)if(!original.keyframes[i].empty()) {
            c::setShapeMaskValue(base,i,c::shapeMaskValue(original.parameters,i));
            if(changed_geometry[i])changed=doc.setMaskKeyframe(id,i,frame_,c::shapeMaskValue(values,i))||changed;
        }
        return doc.setMaskParameters(id,base)||changed;
    });
}
void MaskInspectorWidget::toggleKey(std::size_t property){
    const auto id=selectedMask();if(!id||!edit_)return;
    const auto* mask=document_->findMask(id);const bool exists=std::any_of(mask->keyframes[property].begin(),mask->keyframes[property].end(),[this](const auto& k){return k.frame==frame_;});
    const auto value=fields_[property]->value()==shown_values_[property]
        ? c::shapeMaskValue(c::evaluateShapeMask(mask->parameters,mask->keyframes,frame_),property)
        : fields_[property]->value()/(property==4?1:100);
    edit_([=,this](auto& doc){return exists?doc.removeMaskKeyframe(id,property,frame_):doc.setMaskKeyframe(id,property,frame_,value);});
    if(curve_)curve_(id,property,frame_);
}
}
