#include <QtWidgets>
#include <unistd.h>
#include <signal.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <sys/resource.h>
#include <pwd.h>
#include <cerrno>
#include <cstring>
#include <cmath>
#include <functional>

static QByteArray readFile(const QString &path) { QFile f(path); return f.open(QIODevice::ReadOnly)?f.readAll():QByteArray(); }
static QString bytes(double n) { QStringList u={"o","Kio","Mio","Gio","Tio"}; int i=0; while(n>=1024 &&i<4){n/=1024;++i;} return QString::number(n,'f',i?1:0)+" "+u[i]; }

static QString gpuReadableName(const QString &base,const QString &card,const QString &maker){
 QString target=QFileInfo(base).symLinkTarget();QString addr=target.section('/',-1);QProcess lspci;lspci.start("lspci",{"-s",addr});if(lspci.waitForFinished(300)){QString line=QString::fromLocal8Bit(lspci.readAllStandardOutput()).simplified();int colon=line.indexOf(':');if(colon>=0){QString name=line.mid(colon+1).trimmed();if(!name.isEmpty())return name+" ("+card+")";}}
 return maker+" · "+card;
}
struct Proc {int pid=0,ppid=0; QString name,user,state,command; quint64 ticks=0,start=0,rss=0; double cpu=0; int nice=0; uid_t uid=0;};
static bool readProc(int pid, Proc &p) {
 auto raw=readFile(QString("/proc/%1/stat").arg(pid)); int end=raw.lastIndexOf(')'); if(end<0)return false;
 auto a=raw.mid(end+2).simplified().split(' '); if(a.size()<22)return false;
 p.pid=pid;p.name=QString::fromLocal8Bit(raw.mid(raw.indexOf('(')+1,end-raw.indexOf('(')-1));p.state=a[0];p.ppid=a[1].toInt();
 p.ticks=a[11].toULongLong()+a[12].toULongLong();p.start=a[19].toULongLong();p.rss=qMax(0LL,a[21].toLongLong())*quint64(sysconf(_SC_PAGESIZE));p.nice=a[16].toInt();
 for(auto line:readFile(QString("/proc/%1/status").arg(pid)).split('\n'))if(line.startsWith("Uid:")){p.uid=line.simplified().split(' ').value(1).toUInt();break;}
 auto pw=getpwuid(p.uid);p.user=pw?QString::fromLocal8Bit(pw->pw_name):QString::number(p.uid);
 auto cmd=readFile(QString("/proc/%1/cmdline").arg(pid));cmd.replace('\0',' ');p.command=QString::fromLocal8Bit(cmd).trimmed();return true;
}
struct Sample {double cpu=0;quint64 total=0,available=0,swap=0,swapFree=0;double rx=0,tx=0,rd=0,wr=0;QList<double> cores;QList<Proc> processes;};
class Monitor {
 quint64 oldTotal=0,oldIdle=0,oldRx=0,oldTx=0,oldRd=0,oldWr=0;QList<quint64> oldCoreTotal,oldCoreIdle;QHash<int,Proc> previous;QElapsedTimer clock; qint64 last=0;
 public: Monitor(){clock.start();} Sample sample(){Sample s; auto now=clock.elapsed();double elapsed=qMax(1LL,now-last)/1000.;last=now;
 auto statLines=readFile("/proc/stat").split('\n');auto stat=statLines.value(0).simplified().split(' ');quint64 total=0,idle=0;for(int i=1;i<qMin(9,int(stat.size()));++i)total+=stat[i].toULongLong();idle=stat.value(4).toULongLong()+stat.value(5).toULongLong();
 quint64 delta=total-oldTotal;if(oldTotal&&delta)s.cpu=qBound(0.,100.*(1.-double(idle-oldIdle)/delta),100.);oldTotal=total;oldIdle=idle;int coreIndex=0;for(const auto &raw:statLines){auto a=raw.simplified().split(' ');if(a.size()<5||!a[0].startsWith("cpu")||a[0]=="cpu")continue;bool ok=false;a[0].mid(3).toInt(&ok);if(!ok)continue;quint64 ct=0;for(int i=1;i<qMin(9,a.size());++i)ct+=a[i].toULongLong();quint64 ci=a.value(4).toULongLong()+a.value(5).toULongLong();if(coreIndex>=oldCoreTotal.size()){oldCoreTotal<<ct;oldCoreIdle<<ci;s.cores<<0.;}else{quint64 d=ct-oldCoreTotal[coreIndex];s.cores<<((d&&oldCoreTotal[coreIndex])?qBound(0.,100.*(1.-double(ci-oldCoreIdle[coreIndex])/d),100.):0.);oldCoreTotal[coreIndex]=ct;oldCoreIdle[coreIndex]=ci;}++coreIndex;}
 for(auto l:readFile("/proc/meminfo").split('\n')){auto a=l.simplified().split(' ');auto v=a.value(1).toULongLong()*1024;if(a[0]=="MemTotal:")s.total=v;if(a[0]=="MemAvailable:")s.available=v;if(a[0]=="SwapTotal:")s.swap=v;if(a[0]=="SwapFree:")s.swapFree=v;}
 quint64 rx=0,tx=0,rd=0,wr=0;for(auto l:readFile("/proc/net/dev").split('\n')){int colon=l.indexOf(':');if(colon<0||l.left(colon).trimmed()=="lo")continue;auto a=l.mid(colon+1).simplified().split(' ');rx+=a.value(0).toULongLong();tx+=a.value(8).toULongLong();}
 QDir block("/sys/block");for(auto dev:block.entryList(QDir::Dirs|QDir::NoDotAndDotDot)){if(dev.startsWith("loop")||dev.startsWith("ram")||dev.startsWith("dm-")||dev.startsWith("md"))continue;auto a=readFile(block.filePath(dev+"/stat")).simplified().split(' ');rd+=a.value(2).toULongLong()*512;wr+=a.value(6).toULongLong()*512;}
 if(!previous.isEmpty()){s.rx=rx>=oldRx?(rx-oldRx)/elapsed:0;s.tx=tx>=oldTx?(tx-oldTx)/elapsed:0;s.rd=rd>=oldRd?(rd-oldRd)/elapsed:0;s.wr=wr>=oldWr?(wr-oldWr)/elapsed:0;}oldRx=rx;oldTx=tx;oldRd=rd;oldWr=wr;
 QHash<int,Proc> current;for(auto name:QDir("/proc").entryList(QDir::Dirs|QDir::NoDotAndDotDot)){bool ok;int pid=name.toInt(&ok);if(!ok)continue;Proc p;if(!readProc(pid,p))continue;
 if(previous.contains(pid)&&previous[pid].start==p.start&&delta&&p.ticks>=previous[pid].ticks){p.cpu=qBound(0.,100.*(p.ticks-previous[pid].ticks)/delta,100.);}
 current[pid]=p;s.processes.append(p);}previous=current;return s;
 }
};
class Chart:public QWidget {
 public:QList<double> data;QColor color="#65edce";QString title,value,detail;bool percent=true;
 Chart(QString t,QWidget *p=nullptr):QWidget(p),title(t){setMinimumHeight(170);setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Expanding);}
 void push(double v,QString text,QString sub){data.append(v);while(data.size()>90)data.removeFirst();value=text;detail=sub;update();}
 void paintEvent(QPaintEvent*)override {
 QPainter p(this);p.setRenderHint(QPainter::Antialiasing);
 auto muted=palette().color(QPalette::PlaceholderText);auto border=muted;border.setAlpha(55);
 p.setPen(border);p.setBrush(palette().base());p.drawRoundedRect(rect().adjusted(1,1,-1,-1),4,4);
 QFont f=font();f.setPointSize(10);f.setWeight(QFont::DemiBold);p.setFont(f);p.setPen(palette().text().color());p.drawText(16,27,title);
 f.setPointSize(19);f.setWeight(QFont::Normal);p.setFont(f);p.drawText(QRect(16,35,width()-32,32),Qt::AlignLeft|Qt::AlignVCenter,value);
 f.setPointSize(9);p.setFont(f);p.setPen(muted);p.drawText(16,86,detail);
 QRectF plot(16,111,width()-32,height()-141);if(plot.height()<10)return;
 double maximum=percent?100.:1.;for(double v:data)maximum=qMax(maximum,v*1.15);
 p.drawText(QRectF(plot.left(),93,plot.width(),16),Qt::AlignRight,percent?"100 %":bytes(maximum)+"/s");
 auto grid=muted;grid.setAlpha(35);p.setPen(grid);
 for(int i=0;i<=4;++i){double y=plot.top()+plot.height()*i/4.;p.drawLine(QPointF(plot.left(),y),QPointF(plot.right(),y));}
 for(int i=0;i<=6;++i){double x=plot.left()+plot.width()*i/6.;p.drawLine(QPointF(x,plot.top()),QPointF(x,plot.bottom()));}
 QPainterPath path;double firstX=plot.right();
 for(int i=0;i<data.size();++i){QPointF pt(plot.left()+plot.width()*(90-data.size()+i)/89.,plot.bottom()-plot.height()*data[i]/maximum);if(i==0){path.moveTo(pt);firstX=pt.x();}else path.lineTo(pt);}
 if(data.size()>1){auto fill=path;fill.lineTo(plot.bottomRight());fill.lineTo(firstX,plot.bottom());fill.closeSubpath();auto tint=color;tint.setAlpha(22);p.fillPath(fill,tint);p.setPen(QPen(color,1.6));p.drawPath(path);}
 p.setPen(muted);p.drawText(QRectF(16,height()-24,width()-32,16),Qt::AlignLeft,"Historique");p.drawText(QRectF(16,height()-24,width()-32,16),Qt::AlignRight,"Maintenant");
 }
};

// Custom QTreeWidgetItem for numeric sorting
class NumberTreeItem : public QTreeWidgetItem {
public:
    NumberTreeItem(QTreeWidgetItem *parent) : QTreeWidgetItem(parent) {}
    bool operator<(const QTreeWidgetItem &other) const override {
        int col = treeWidget() ? treeWidget()->sortColumn() : 0;
        QVariant myData = data(col, Qt::UserRole);
        QVariant otherData = other.data(col, Qt::UserRole);
        if (myData.isValid() && otherData.isValid())
            return myData.toDouble() < otherData.toDouble();
        return text(col) < other.text(col);
    }
};

static const QList<QPair<QString,int>> priorityLevels = {
 {"Très basse",19},{"Basse",10},{"Normale",0},{"Haute",-5},{"Très haute",-10}
};
static QString priorityName(int value) {
 for(const auto &level:priorityLevels)if(level.second==value)return level.first;
 return value>10?"Très basse":value>0?"Basse":value>-5?"Haute":value>-10?"Haute":"Très haute";
}
class ProcessIcons {
 QHash<QString,QIcon> known,cache;
 QIcon resolve(QString name){
 if(name.isEmpty())return {};
 if(name.startsWith('/'))return QIcon(name);
 auto themed=QIcon::fromTheme(name);if(!themed.isNull())return themed;
 QStringList dirs{QDir::homePath()+"/.local/share/pixmaps","/usr/share/pixmaps"};
 for(auto root:QStringList{QDir::homePath()+"/.local/share/icons","/usr/share/icons","/var/lib/flatpak/exports/share/icons",QDir::homePath()+"/.local/share/flatpak/exports/share/icons"}){
 for(auto size:{"64x64","48x48","128x128","256x256","scalable","32x32","512x512"})dirs<<root+"/hicolor/"+size+"/apps";
 }
 dirs<<"/usr/share/icons/Papirus/64x64/apps"<<"/usr/share/icons/breeze/apps/48";
 for(const auto &dir:dirs)for(auto ext:{".svg",".png",".xpm"}){QString path=dir+"/"+name+ext;if(QFileInfo::exists(path))return QIcon(path);}
 return {};
 }

 public: ProcessIcons(){
 QStringList dirs=QStandardPaths::standardLocations(QStandardPaths::ApplicationsLocation);
 dirs << QDir::homePath()+"/.local/share/flatpak/exports/share/applications" << "/var/lib/flatpak/exports/share/applications" << "/var/lib/snapd/desktop/applications";
 dirs.removeDuplicates();
 for(const auto &dir:dirs){QDirIterator it(dir,{"*.desktop"},QDir::Files,QDirIterator::Subdirectories);
 while(it.hasNext()){QString path=it.next();QHash<QString,QString> fields;bool main=false;
 for(auto raw:readFile(path).split('\n')){QString line=QString::fromUtf8(raw).trimmed();if(line.startsWith('[')){main=line=="[Desktop Entry]";continue;}int eq=line.indexOf('=');if(main&&eq>0)fields[line.left(eq)]=line.mid(eq+1);}
 QString iconName=fields.value("Icon");QIcon icon=resolve(iconName);if(icon.isNull())continue;
 QStringList keys{QFileInfo(path).completeBaseName(),fields.value("StartupWMClass"),fields.value("Name")};
 auto args=QProcess::splitCommand(fields.value("Exec"));if(!args.isEmpty()){QString exe=QFileInfo(args.first()).fileName();if(exe!="env"&&exe!="flatpak"&&exe!="sh"&&exe!="bash"&&exe!="python3")keys<<exe;}
 for(auto key:keys){key=key.toLower();if(!key.isEmpty()&&!known.contains(key))known.insert(key,icon);}
 }}
 known.insert("pulse",QIcon(":/logo.svg"));
 }
 bool hasKnownIcon(const Proc &p) {
     QString exe=QFileInfo(QFileInfo(QString("/proc/%1/exe").arg(p.pid)).symLinkTarget()).fileName().toLower();
     for(auto candidate:{exe,p.name.toLower(),exe+"-browser"})
         if(known.contains(candidate))return true;
     return false;
 }
 QIcon get(const Proc &p){QString key=p.name+"|"+p.command.section(' ',0,0);if(cache.contains(key))return cache[key];
 QString exe=QFileInfo(QFileInfo(QString("/proc/%1/exe").arg(p.pid)).symLinkTarget()).fileName().toLower();
 QIcon icon;for(auto candidate:{exe,p.name.toLower(),exe+"-browser"}){if(known.contains(candidate)){icon=known[candidate];break;}icon=resolve(candidate);if(!icon.isNull())break;}
 if(icon.isNull())icon=QIcon::fromTheme("application-x-executable",qApp->style()->standardIcon(QStyle::SP_FileIcon));
 if(cache.size()>4096){cache.clear();}
 cache.insert(key,icon);return icon;
 }
 const QHash<QString,QIcon>& knownIcons() const { return known; }
};

// Category enum for process classification
enum Category { CAT_APPS=0, CAT_SYSTEM=1, CAT_MINE=2, CAT_OTHERS=3 };

class Window:public QMainWindow {
 ProcessIcons icons;Monitor monitor;Sample latest;QSettings settings{"Pulse","TaskManager"};QTimer timer;
 QTreeWidget *table;QLineEdit *search;QCheckBox *mine;QLabel *status;QList<Chart*> charts;QStackedWidget *pages;
 QColor accent;QString theme;bool compact=false;bool english=false;QLabel *gpuInfo=nullptr,*tempInfo=nullptr;QTabWidget *gpuTabs=nullptr;QList<Chart*> coreCharts,gpuCharts;QList<QString> gpuPaths;QHash<int,Proc> rows;
 QTreeWidgetItem *catItems[4]={nullptr,nullptr,nullptr,nullptr};
 bool collapseHandled=false; // guard against recursive expand
 QString T(const QString &fr,const QString &en) const { return english?en:fr; }

 public:Window(){setWindowTitle("Pulse · Gestionnaire de tâches");setWindowIcon(QIcon(":/logo.svg"));resize(1080,740);setMinimumSize(920,620);
 accent=QColor(settings.value("accent","#6698d0").toString());theme=settings.value("theme","Minuit").toString();compact=settings.value("compact",false).toBool();english=settings.value("language","fr").toString()=="en";
 auto root=new QWidget;setCentralWidget(root);auto layout=new QVBoxLayout(root);layout->setContentsMargins(0,0,0,0);layout->setSpacing(0);
 auto toolbar=new QWidget;toolbar->setObjectName("toolbar");auto nav=new QHBoxLayout(toolbar);nav->setContentsMargins(18,0,18,0);nav->setSpacing(4);
 auto brand=new QLabel;brand->setPixmap(QIcon(":/logo.svg").pixmap(28,28));nav->addWidget(brand);auto name=new QLabel("Pulse");name->setObjectName("appName");nav->addWidget(name);nav->addSpacing(24);
 pages=new QStackedWidget;auto group=new QButtonGroup(this);int idx=0;
 for(auto label:{T("Performances","Performance"),T("Processus","Processes"),T("Réglages","Settings")}){auto b=new QPushButton(label);b->setCheckable(true);b->setObjectName("nav");group->addButton(b,idx);nav->addWidget(b);if(idx==1)b->setChecked(true);++idx;}
 connect(group,&QButtonGroup::idClicked,pages,&QStackedWidget::setCurrentIndex);nav->addStretch();auto host=new QLabel(QSysInfo::machineHostName());host->setObjectName("muted");nav->addWidget(host);layout->addWidget(toolbar);layout->addWidget(pages,1);
 auto overview=new QWidget;auto ov=new QVBoxLayout(overview);ov->setContentsMargins(20,18,20,18);heading(ov,T("Performances","Performance"),T("Surveillance matérielle","Hardware monitoring"));auto perfTabs=new QTabWidget;auto general=new QWidget;auto generalLayout=new QVBoxLayout(general);auto grid=new QGridLayout;int c=0;for(auto title:{T("Processeur","CPU"),T("Mémoire","Memory"),T("Réseau","Network"),T("Disques","Disks")}){auto chart=new Chart(title);chart->percent=c<2;charts<<chart;grid->addWidget(chart,c/2,c%2);++c;}generalLayout->addLayout(grid,1);perfTabs->addTab(general,T("Vue générale","Overview"));auto cpuPage=new QWidget;auto cpuLayout=new QVBoxLayout(cpuPage);auto info=new QLabel;info->setWordWrap(true);QString cpu;for(auto line:readFile("/proc/cpuinfo").split('\n'))if(line.startsWith("model name")){cpu=QString::fromLocal8Bit(line.mid(line.indexOf(':')+1)).trimmed();break;}info->setText(cpu+"\n"+QString::number(sysconf(_SC_NPROCESSORS_ONLN))+" processeurs logiques · "+QSysInfo::prettyProductName()+" · "+QSysInfo::kernelVersion());info->setObjectName("muted");cpuLayout->addWidget(info);auto coreScroll=new QScrollArea;coreScroll->setWidgetResizable(true);auto corePage=new QWidget;auto coreGrid=new QGridLayout(corePage);int coreCount=int(sysconf(_SC_NPROCESSORS_ONLN));for(int i=0;i<coreCount;++i){auto chart=new Chart(T("Cœur ","Core ")+QString::number(i));chart->percent=true;coreCharts<<chart;coreGrid->addWidget(chart,i/2,i%2);}coreScroll->setWidget(corePage);cpuLayout->addWidget(coreScroll,1);perfTabs->addTab(cpuPage,T("Processeur","CPU"));auto gpuPage=new QWidget;auto gpuLayout=new QVBoxLayout(gpuPage);gpuTabs=new QTabWidget;for(const auto &card:QDir("/sys/class/drm").entryList({"card*"},QDir::Dirs)){QString base="/sys/class/drm/"+card+"/device/";QString vendor=QString::fromUtf8(readFile(base+"vendor")).trimmed().toLower();QString maker=vendor.contains("1002")?"AMD":vendor.contains("10de")?"NVIDIA":vendor.contains("8086")?"Intel":"GPU";if(!QFileInfo::exists(base+"vendor"))continue;gpuPaths<<base;auto page=new QWidget;auto lay=new QVBoxLayout(page);auto chart=new Chart(maker+" · "+card);chart->percent=true;gpuCharts<<chart;lay->addWidget(chart);auto label=new QLabel;label->setObjectName("muted");label->setWordWrap(true);lay->addWidget(label);lay->addStretch();gpuTabs->addTab(page,gpuReadableName(base,card,maker));}if(gpuTabs->count()==0){gpuInfo=new QLabel;gpuInfo->setText(T("Aucune carte graphique détectée.","No graphics card detected."));gpuLayout->addWidget(gpuInfo);}else gpuLayout->addWidget(gpuTabs);gpuLayout->addStretch();perfTabs->addTab(gpuPage,T("Cartes graphiques","Graphics cards"));auto tempPage=new QWidget;auto tempLayout=new QVBoxLayout(tempPage);tempInfo=new QLabel;tempInfo->setWordWrap(true);tempInfo->setTextInteractionFlags(Qt::TextSelectableByMouse);tempLayout->addWidget(tempInfo);tempLayout->addStretch();perfTabs->addTab(tempPage,T("Températures","Temperatures"));ov->addWidget(perfTabs,1);pages->addWidget(overview);

 // --- Processes page ---
 auto processes=new QWidget;auto pv=new QVBoxLayout(processes);pv->setContentsMargins(20,18,20,18);heading(pv,T("Processus","Processes"),"");
 auto toolsRow=new QHBoxLayout;search=new QLineEdit;search->setClearButtonEnabled(true);search->setPlaceholderText(T("Rechercher un processus…  (Ctrl+F)","Search processes…  (Ctrl+F)"));toolsRow->addWidget(search,1);mine=new QCheckBox(T("Mes processus","My processes"));mine->setChecked(settings.value("mine",false).toBool());toolsRow->addWidget(mine);pv->addLayout(toolsRow);

 table=new QTreeWidget;table->setColumnCount(7);table->setIconSize(QSize(20,20));table->setContextMenuPolicy(Qt::CustomContextMenu);
 connect(table,&QWidget::customContextMenuRequested,this,[this](QPoint pos){processMenu(pos);});
 table->setHeaderLabels({T("Nom","Name"),"PID","CPU %",T("Mémoire","Memory"),T("Utilisateur","User"),T("État","State"),T("Priorité","Priority")});
 table->setSelectionBehavior(QAbstractItemView::SelectRows);table->setSelectionMode(QAbstractItemView::SingleSelection);
 table->setEditTriggers(QAbstractItemView::NoEditTriggers);
 table->header()->setDefaultAlignment(Qt::AlignLeft|Qt::AlignVCenter);
 table->setRootIsDecorated(true);table->setUniformRowHeights(true);
 table->setAnimated(true);table->setAllColumnsShowFocus(true);
 table->setAlternatingRowColors(true);
 table->header()->setSectionResizeMode(0,QHeaderView::Stretch);
 for(int i=1;i<7;++i)table->setColumnWidth(i,i==4?115:i==6?105:85);
 table->setSortingEnabled(true);table->sortItems(2,Qt::DescendingOrder);
 pv->addWidget(table,1);

 auto actions=new QHBoxLayout;
 for(auto pair:QList<QPair<QString,int>>{{T("Terminer","End"),SIGTERM},{T("Forcer l’arrêt","Force stop"),SIGKILL},{T("Suspendre","Suspend"),SIGSTOP},{T("Reprendre","Resume"),SIGCONT}}){
     auto b=new QPushButton(pair.first);if(pair.second==SIGKILL)b->setObjectName("danger");actions->addWidget(b);connect(b,&QPushButton::clicked,this,[this,pair]{signalSelected(pair.second);});
 }
 auto priority=new QPushButton("Priorité…");actions->addWidget(priority);connect(priority,&QPushButton::clicked,this,[this]{changePriority();});
 auto details=new QPushButton(T("Détails","Details"));actions->addWidget(details);connect(details,&QPushButton::clicked,this,[this]{showDetails();});
 auto exportCsv=new QPushButton(T("Exporter CSV","Export CSV"));actions->addWidget(exportCsv);connect(exportCsv,&QPushButton::clicked,this,[this]{exportProcessesCsv();});
 pv->addLayout(actions);pages->addWidget(processes);

 connect(table,&QTreeWidget::itemDoubleClicked,this,[this](QTreeWidgetItem *item,int){
     if(item&&item->parent())showDetails(); // only leaf items
 });
 connect(search,&QLineEdit::textChanged,this,[this]{populate();});
 connect(mine,&QCheckBox::toggled,this,[this](bool v){settings.setValue("mine",v);populate();});

 // --- Settings page ---
 auto prefs=new QWidget;auto pre=new QVBoxLayout(prefs);pre->setContentsMargins(20,18,20,18);heading(pre,T("Réglages","Settings"),T("Les modifications sont enregistrées automatiquement.","Changes are saved automatically."));auto form=new QFormLayout;form->setVerticalSpacing(16);auto language=new QComboBox;language->addItem("Français","fr");language->addItem("English","en");language->setCurrentIndex(language->findData(english?"en":"fr"));form->addRow(T("Langue","Language"),language);connect(language,QOverload<int>::of(&QComboBox::currentIndexChanged),this,[this,language](int){settings.setValue("language",language->currentData().toString());QMessageBox::information(this,T("Langue","Language"),T("Redémarre Pulse pour appliquer la langue.","Restart Pulse to apply the language."));});auto themes=new QComboBox;themes->addItems({"Minuit","Ardoise","Clair"});themes->setCurrentText(theme);form->addRow(T("Apparence","Appearance"),themes);connect(themes,&QComboBox::currentTextChanged,this,[this](QString v){theme=v;settings.setValue("theme",v);applyTheme();});auto color=new QPushButton(T("Choisir couleur d’accent…","Choose accent color…"));form->addRow(T("Couleur","Color"),color);connect(color,&QPushButton::clicked,this,[this]{auto c=QColorDialog::getColor(accent,this,"Couleur d'accent");if(c.isValid()){accent=c;settings.setValue("accent",c.name());applyTheme();}});
 auto interval=new QComboBox;interval->addItem("Rapide · 0,5 seconde",500);interval->addItem("Équilibré · 1 seconde",1000);interval->addItem("Économe · 2 secondes",2000);interval->addItem("Très économe · 5 secondes",5000);int ms=settings.value("interval",1000).toInt();int ii=interval->findData(ms);interval->setCurrentIndex(ii<0?1:ii);form->addRow(T("Actualisation","Refresh"),interval);connect(interval,&QComboBox::currentIndexChanged,this,[this,interval]{int ms=interval->currentData().toInt();settings.setValue("interval",ms);timer.setInterval(ms);});auto density=new QCheckBox("Lignes compactes");density->setChecked(compact);form->addRow(T("Densité","Density"),density);connect(density,&QCheckBox::toggled,this,[this](bool v){compact=v;settings.setValue("compact",v);applyTheme();});
 auto top=new QCheckBox("Garder fenêtre au premier plan");form->addRow(T("Fenêtre","Window"),top);connect(top,&QCheckBox::toggled,this,[this](bool v){setWindowFlag(Qt::WindowStaysOnTopHint,v);show();});
 auto cols=new QWidget;auto cl=new QHBoxLayout(cols);cl->setContentsMargins(0,0,0,0);
 for(int i=1;i<7;++i){
     auto cb=new QCheckBox(table->headerItem()->text(i));
     bool visible=settings.value("column"+QString::number(i),true).toBool();
     cb->setChecked(visible);table->setColumnHidden(i,!visible);cl->addWidget(cb);
     connect(cb,&QCheckBox::toggled,this,[this,i](bool v){table->setColumnHidden(i,!v);settings.setValue("column"+QString::number(i),v);});
 }
 form->addRow(T("Colonnes","Columns"),cols);pre->addLayout(form);pre->addSpacing(24);
 auto help=new QLabel("CPU processus : part de la capacité totale de la machine.\nCourbes : 90 dernières mesures. Réseau : interfaces hors boucle locale.\nDisques : débit cumulé des périphériques physiques.\nPriorité Linux : −20 = haute, 19 = basse. Droits système respectés.\nCtrl+F : recherche processus · Espace : figer / reprendre les mesures.");help->setWordWrap(true);help->setObjectName("muted");pre->addWidget(help);pre->addStretch();pages->addWidget(prefs);

 status=new QLabel;statusBar()->addWidget(status,1);auto pause=new QPushButton(T("Figer","Freeze"));pause->setCheckable(true);statusBar()->addPermanentWidget(pause);connect(pause,&QPushButton::toggled,this,[this,pause](bool v){if(v)timer.stop();else timer.start();pause->setText(v?T("Reprendre","Resume"):T("Figer","Freeze"));statusBar()->showMessage(v?"Mesures figées":"Mesures en direct",2000);});auto space=new QShortcut(QKeySequence(Qt::Key_Space),this);connect(space,&QShortcut::activated,pause,&QPushButton::click);auto find=new QShortcut(QKeySequence::Find,this);connect(find,&QShortcut::activated,this,[this]{pages->setCurrentIndex(1);search->setFocus();});connect(pages,&QStackedWidget::currentChanged,this,[group](int i){group->button(i)->setChecked(true);});
 pages->setCurrentIndex(1);applyTheme();restoreGeometry(settings.value("geometry").toByteArray());timer.setInterval(interval->currentData().toInt());connect(&timer,&QTimer::timeout,this,[this]{refresh();});refresh();timer.start();}

 void heading(QVBoxLayout *l,QString title,QString sub){auto h=new QLabel(title);h->setObjectName("heading");l->addWidget(h);if(!sub.isEmpty()){auto s=new QLabel(sub);s->setObjectName("muted");l->addWidget(s);}l->addSpacing(10);}

 void applyTheme(){
 bool light=theme=="Clair";QString bg=light?"#f4f4f3":theme=="Ardoise"?"#303438":"#242628";
 QString base=light?"#ffffff":theme=="Ardoise"?"#363b40":"#292c2f";
 QString text=light?"#24282d":"#e4e6e8",muted=light?"#666c73":"#a4a9b0";
 QString border=light?"#d3d6d9":"#42464b",alternate=light?"#fafafa":"#2c2f32";
 QString hover=light?"#e9ebed":"#363b40";
 QColor selection=accent;selection.setAlpha(35);QString selectedColor=QString("rgba(%1,%2,%3,35)").arg(accent.red()).arg(accent.green()).arg(accent.blue());
 QPalette p;p.setColor(QPalette::Window,QColor(bg));p.setColor(QPalette::Base,QColor(base));p.setColor(QPalette::AlternateBase,QColor(alternate));p.setColor(QPalette::WindowText,QColor(text));p.setColor(QPalette::Text,QColor(text));p.setColor(QPalette::Button,QColor(base));p.setColor(QPalette::ButtonText,QColor(text));p.setColor(QPalette::PlaceholderText,QColor(muted));p.setColor(QPalette::Highlight,selection);p.setColor(QPalette::HighlightedText,QColor(text));qApp->setPalette(p);
 QString css=QString::fromUtf8(readFile(":/style.qss"));
 for(auto pair:QList<QPair<QString,QString>>{{"@bg",bg},{"@base",base},{"@text",text},{"@muted",muted},{"@border",border},{"@alternate",alternate},{"@hover",hover},{"@accent",accent.name()},{"@selection",selectedColor},{"@danger",light?"#aa3636":"#e39898"}})css.replace(pair.first,pair.second);
 setStyleSheet(css);
 // Adapt row height for QTreeWidget
 table->setStyleSheet(QString("QTreeWidget { font-size: 12px; } QTreeWidget::item { padding: %1px 4px; }").arg(compact?1:4));
 for(auto chart:charts){chart->color=accent;chart->update();}
 }

 Category classifyProc(const Proc &p) {
     if(p.uid==0)return CAT_SYSTEM;
     if(p.uid==getuid()&&icons.hasKnownIcon(p))return CAT_APPS;
     if(p.uid==getuid())return CAT_MINE;
     return CAT_OTHERS;
 }

 double gpuBusy(const QString &base,int index) const {for(const auto &file:QStringList{"gpu_busy_percent","gt_busy_percent","busy_percent"}){bool ok=false;double v=QString::fromUtf8(readFile(base+file)).trimmed().toDouble(&ok);if(ok&&v>=0&&v<=100)return v;}QProcess nvidia;nvidia.start("nvidia-smi",{"--query-gpu=utilization.gpu","--format=csv,noheader,nounits"});if(nvidia.waitForFinished(250)){auto values=QString::fromLocal8Bit(nvidia.readAllStandardOutput()).trimmed().split('\n');if(index<values.size()){bool ok=false;double v=values[index].trimmed().toDouble(&ok);if(ok)return v;}}return -1;}
 QString gpuText() const {QStringList lines;int index=0;for(const auto &path:QDir("/sys/class/drm").entryList({"card*"},QDir::Dirs)){QString base="/sys/class/drm/"+path+"/device/";auto vendor=QString::fromUtf8(readFile(base+"vendor")).trimmed();auto busy=gpuBusy(base,index);auto used=QString::fromUtf8(readFile(base+"mem_info_vram_used")).trimmed();auto total=QString::fromUtf8(readFile(base+"mem_info_vram_total")).trimmed();if(vendor.isEmpty()){++index;continue;}QString line=gpuReadableName(base,path,"GPU");if(busy>=0)line+="\nActivité / Activity : "+QString::number(busy,'f',1)+" %";else line+="\nActivité : pilote sans compteur disponible / driver counter unavailable";if(!used.isEmpty()&&!total.isEmpty())line+="\nVRAM : "+bytes(used.toULongLong())+" / "+bytes(total.toULongLong());lines<<line;++index;}return lines.isEmpty()?T("Aucune carte graphique détectée.","No graphics card detected."):lines.join("\n\n");}
 QString tempText() const {QStringList cpuLines,gpuLines;auto add=[&](QStringList &out,const QString &name,const QString &raw){bool ok=false;double value=raw.trimmed().toDouble(&ok);if(!ok)return;if(value>1000)value/=1000.;if(value< -40||value>130)return;out<<name+" — "+QString::number(value,'f',1)+" °C";};for(const auto &hw:QDir("/sys/class/hwmon").entryList({"hwmon*"},QDir::Dirs)){QString base="/sys/class/hwmon/"+hw+"/";QString kind=QString::fromUtf8(readFile(base+"name")).trimmed().toLower();bool isCpu=kind=="coretemp"||kind=="k10temp"||kind=="zenpower";bool isGpu=kind=="nvidia"||kind=="amdgpu"||kind=="nouveau"||kind=="i915";if(!isCpu&&!isGpu)continue;for(const auto &input:QDir(base).entryList({"temp*_input"},QDir::Files)){QString stem=input.left(input.size()-6);QString label=QString::fromUtf8(readFile(base+stem+"_label")).trimmed();if(label.isEmpty())label=stem;auto title=(isGpu?T("GPU ","GPU "):T("CPU ","CPU "))+label;add(isGpu?gpuLines:cpuLines,title,QString::fromUtf8(readFile(base+input)));}}if(cpuLines.isEmpty())for(const auto &zone:QDir("/sys/class/thermal").entryList({"thermal_zone*"},QDir::Dirs)){QString base="/sys/class/thermal/"+zone+"/";auto type=QString::fromUtf8(readFile(base+"type")).trimmed();if(type.compare("TCPU",Qt::CaseInsensitive)==0||type.compare("x86_pkg_temp",Qt::CaseInsensitive)==0)add(cpuLines,T("CPU","CPU"),QString::fromUtf8(readFile(base+"temp")));}if(cpuLines.isEmpty())cpuLines<<T("CPU : sonde indisponible","CPU: sensor unavailable");if(gpuLines.isEmpty())gpuLines<<T("GPU : sonde indisponible (pilote)","GPU: sensor unavailable (driver)");return (cpuLines+gpuLines).join("\n");}
 void refresh(){latest=monitor.sample();double mem=latest.total?100.*(latest.total-latest.available)/latest.total:0;charts[0]->push(latest.cpu,QString::number(latest.cpu,'f',1)+" %",QString::number(sysconf(_SC_NPROCESSORS_ONLN))+" processeurs logiques");charts[1]->push(mem,QString::number(mem,'f',1)+" %",bytes(latest.total-latest.available)+" / "+bytes(latest.total));charts[2]->push(latest.rx+latest.tx,bytes(latest.rx+latest.tx)+"/s","↓ "+bytes(latest.rx)+"/s    ↑ "+bytes(latest.tx)+"/s");charts[3]->push(latest.rd+latest.wr,bytes(latest.rd+latest.wr)+"/s","Lecture "+bytes(latest.rd)+"/s · Écriture "+bytes(latest.wr)+"/s");if(gpuInfo)gpuInfo->setText(gpuText());if(tempInfo)tempInfo->setText(tempText());for(int i=0;i<coreCharts.size()&&i<latest.cores.size();++i)coreCharts[i]->push(latest.cores[i],QString::number(latest.cores[i],'f',1)+" %",T("Utilisation cœur","Core usage"));for(int i=0;i<gpuCharts.size()&&i<gpuPaths.size();++i){auto base=gpuPaths[i];double busy=gpuBusy(gpuPaths[i],i);if(busy>=0)gpuCharts[i]->push(busy,QString::number(busy,'f',1)+" %",T("Activité GPU","GPU activity"));else gpuCharts[i]->push(0,"—",T("Compteur indisponible","Counter unavailable"));}status->setText(QString("  %1 processus  ·  CPU %2 %  ·  RAM %3  ·  Swap %4 / %5").arg(latest.processes.size()).arg(latest.cpu,0,'f',1).arg(bytes(latest.total-latest.available),bytes(latest.swap-latest.swapFree),bytes(latest.swap)));populate();}

 int selected(){
     auto items=table->selectedItems();
     if(items.isEmpty())return -1;
     auto item=items[0];
     // Must be a leaf (has a parent category)
     if(!item->parent())return -1;
     return item->text(1).toInt();
 }

 void populate(){
     int pid=selected();
     int scroll=table->verticalScrollBar()->value();
     auto sortCol=table->header()->sortIndicatorSection();
     auto sortOrder=table->header()->sortIndicatorOrder();
     table->setSortingEnabled(false);
     table->setUpdatesEnabled(false);

     // Remember expanded state
     bool expanded[4]={true,true,true,true};
     for(int i=0;i<4;++i)if(catItems[i])expanded[i]=catItems[i]->isExpanded();

     table->clear();
     rows.clear();
     for(int i=0;i<4;++i)catItems[i]=nullptr;

     // Category labels (base names without count)
     QStringList catNames{T("Applications","Applications"),T("Services système","System services"),T("Mes processus","My processes"),T("Autres","Other")};

     // Create category items
     for(int i=0;i<4;++i){
         catItems[i]=new QTreeWidgetItem(table);
         catItems[i]->setText(0,QString(catNames[i]));
         catItems[i]->setFlags(Qt::ItemIsEnabled); // not selectable
         catItems[i]->setData(0,Qt::UserRole+1,true);
         catItems[i]->setExpanded(expanded[i]);
         // Bold font, muted foreground
         QFont f=catItems[i]->font(0);f.setBold(true);
         for(int c=0;c<7;++c){catItems[i]->setFont(c,f);}
     }

     auto query=search->text();
     QHash<int,QTreeWidgetItem*> processItems;
     QHash<int,Category> processCategories;

     for(const auto &p:latest.processes){
         if(mine->isChecked()&&p.uid!=getuid())continue;
         if(!(p.name+" "+QString::number(p.pid)+" "+p.user+" "+p.command).contains(query,Qt::CaseInsensitive))continue;

         Category cat=classifyProc(p);
         // When "Mes processus" filter is on, hide Others and System
         if(mine->isChecked()&&cat!=CAT_APPS&&cat!=CAT_MINE)continue;

         auto *parentItem=catItems[cat];
         auto item=new NumberTreeItem(parentItem);
         item->setFlags(Qt::ItemIsEnabled|Qt::ItemIsSelectable);
         item->setData(0,Qt::UserRole+1,false);
         item->setIcon(0,icons.get(p));
         item->setText(0,p.name);
         item->setToolTip(0,p.command);
         item->setText(1,QString::number(p.pid));
         item->setData(1,Qt::UserRole,p.pid);
         item->setText(2,QString::number(p.cpu,'f',1));
         item->setData(2,Qt::UserRole,p.cpu);
         item->setText(3,bytes(p.rss));
         item->setData(3,Qt::UserRole,(double)p.rss);
         item->setText(4,p.user);
         QString state=p.state=="R"?"Actif":p.state=="S"?"Repos":p.state=="T"?"Suspendu":p.state=="Z"?"Zombie":p.state;
         item->setText(5,state);
         item->setText(6,priorityName(p.nice));
         item->setData(6,Qt::UserRole,p.nice);
         // Align numeric columns right
         for(int col:{1,2,3,6})item->setTextAlignment(col,Qt::AlignRight|Qt::AlignVCenter);
         rows[p.pid]=p;
         processItems.insert(p.pid,item);
         processCategories.insert(p.pid,cat);
     }

     // Rebuild parent → child nesting across visual categories. A missing
     // parent stays directly below the category (it may have exited between
     // two procfs reads). This keeps arrows stable while processes change.
     for(const auto &p:latest.processes){
         auto child=processItems.value(p.pid,nullptr);
         auto parent=processItems.value(p.ppid,nullptr);
         // PID 1 is the kernel's global supervisor, not an application group.
         // Keep its direct children as category roots so Applications and
         // Mes processus never disappear under “Services système”.
         if(child && parent && child!=parent && p.ppid>1 &&
            processCategories.value(p.pid)==processCategories.value(p.ppid)){
             if(auto old=child->parent())old->takeChild(old->indexOfChild(child));
             parent->addChild(child);
         }
     }

     // Update category labels with counts, hide empty categories
     for(int i=0;i<4;++i){
         int count=catItems[i]->childCount();
         catItems[i]->setText(0,QString("%1 (%2)").arg(catNames[i]).arg(count));
         catItems[i]->setHidden(count==0);
         if(qEnvironmentVariableIsSet("PULSE_DEBUG_TREE"))qWarning().noquote()<<catNames[i]<<"roots"<<count;
     }

     table->setSortingEnabled(true);
     // Sort children within each category
     table->sortItems(sortCol,sortOrder);

     // Restore selection
     if(pid>0){
         for(int i=0;i<4;++i){
             for(int j=0;j<catItems[i]->childCount();++j){
                 if(catItems[i]->child(j)->text(1).toInt()==pid){
                     table->setCurrentItem(catItems[i]->child(j));
                     break;
                 }
             }
         }
     }
     table->verticalScrollBar()->setValue(scroll);
     table->setUpdatesEnabled(true);
 }

 bool target(Proc &p){int pid=selected();if(pid<0){QMessageBox::information(this,"Processus","Sélectionne un processus.");return false;}p=rows.value(pid);return true;}
 bool same(const Proc &p){Proc fresh;if(!readProc(p.pid,fresh)||fresh.start!=p.start){QMessageBox::warning(this,"Processus disparu","Processus terminé ou PID réutilisé. Action annulée.");return false;}return true;}
 bool sendSignal(int pid,int sig){
     int fd=syscall(SYS_pidfd_open,pid,0); if(fd<0)return false;
     int result=syscall(SYS_pidfd_send_signal,fd,sig,nullptr,0); ::close(fd); return result==0;
 }
 QList<int> categoryPids(QTreeWidgetItem *category) const {
     QList<int> result;
     std::function<void(QTreeWidgetItem*)> collect=[&](QTreeWidgetItem *item){
         int pid=item->text(1).toInt(); if(pid>1&&pid!=getpid())result.append(pid);
         for(int i=0;i<item->childCount();++i)collect(item->child(i));
     };
     for(int i=0;i<category->childCount();++i)collect(category->child(i));
     return result;
 }
 void stopCategory(QTreeWidgetItem *category,int sig){
     auto pids=categoryPids(category); if(pids.isEmpty())return;
     QString name=category->text(0); int paren=name.indexOf('('); if(paren>0)name=name.left(paren).trimmed();
     QString action=sig==SIGKILL?"forcer l'arrêt":"terminer";
     if(QMessageBox::question(this,"Confirmer le groupe",QString("%1 tous les %2 processus de « %3 » ?").arg(action).arg(pids.size()).arg(name),QMessageBox::Yes|QMessageBox::No,QMessageBox::No)!=QMessageBox::Yes)return;
     int sent=0; for(int pid:pids)if(sendSignal(pid,sig))++sent;
     statusBar()->showMessage(QString("Signal envoyé à %1/%2 processus").arg(sent).arg(pids.size()),5000);
     refresh();
 }

 void signalSelected(int sig){Proc p;if(!target(p))return;if(p.pid<=1||p.pid==getpid()){QMessageBox::warning(this,"Action indisponible","Ce processus ne peut pas être contrôlé ici.");return;}QString action=sig==SIGTERM?"Terminer":sig==SIGKILL?"Forcer l'arrêt de":sig==SIGSTOP?"Suspendre":"Reprendre";QString warning=(sig==SIGTERM||sig==SIGKILL)?"\nLes données non enregistrées peuvent être perdues.":"";if(QMessageBox::question(this,"Confirmer",action+" « "+p.name+" » (PID "+QString::number(p.pid)+") ?"+warning)!=QMessageBox::Yes)return;int fd=syscall(SYS_pidfd_open,p.pid,0);if(fd<0){QMessageBox::warning(this,"Action refusée",QString::fromLocal8Bit(strerror(errno)));return;}if(!same(p)){::close(fd);return;}int result=syscall(SYS_pidfd_send_signal,fd,sig,nullptr,0);int error=errno;::close(fd);if(result!=0)QMessageBox::warning(this,"Action refusée",QString::fromLocal8Bit(strerror(error)));else statusBar()->showMessage("Signal envoyé à "+p.name,4000);}

 void applyPriority(const Proc &p,int value){
 if(!same(p))return;
 if(setpriority(PRIO_PROCESS,p.pid,value)!=0){int error=errno;QString reason=(error==EACCES||error==EPERM)?"Linux refuse ce changement avec les droits actuels. Augmenter la priorité peut nécessiter des droits supplémentaires.":QString::fromLocal8Bit(strerror(error));QMessageBox::warning(this,"Priorité inchangée",reason);return;}
 statusBar()->showMessage(p.name+" : priorité "+priorityName(value),4000);refresh();
 }

 void changePriority(){Proc p;if(!target(p))return;QStringList labels;int current=2;for(int i=0;i<priorityLevels.size();++i){labels<<priorityLevels[i].first;if(priorityLevels[i].first==priorityName(p.nice))current=i;}
 bool ok;QString label=QInputDialog::getItem(this,"Priorité du processus",p.name+" — priorité",labels,current,false,&ok);if(ok)applyPriority(p,priorityLevels[labels.indexOf(label)].second);
 }

 void processMenu(QPoint pos){
 auto item=table->itemAt(pos);if(!item)return;
 if(!item->parent()){
     if(!item->data(0,Qt::UserRole+1).toBool())return;
     QMenu menu(this);auto title=menu.addAction(item->text(0));title->setEnabled(false);menu.addSeparator();
     connect(menu.addAction("Terminer tout le groupe"),&QAction::triggered,this,[this,item]{stopCategory(item,SIGTERM);});
     auto force=menu.addAction("Forcer l'arrêt de tout le groupe");force->setObjectName("danger");connect(force,&QAction::triggered,this,[this,item]{stopCategory(item,SIGKILL);});
     menu.addSeparator();connect(menu.addAction(item->isExpanded()?"Replier":"Développer"),&QAction::triggered,this,[item]{item->setExpanded(!item->isExpanded());});
     menu.exec(table->viewport()->mapToGlobal(pos));
     return;
 }
 table->setCurrentItem(item);
 Proc p=rows.value(selected());
 bool running=timer.isActive();timer.stop();QMenu menu(this);auto title=menu.addAction(icons.get(p),p.name+" · "+QString::number(p.pid));title->setEnabled(false);menu.addSeparator();
 for(auto pair:QList<QPair<QString,int>>{{T("Terminer","End"),SIGTERM},{T("Forcer l’arrêt","Force stop"),SIGKILL},{T("Suspendre","Suspend"),SIGSTOP},{T("Reprendre","Resume"),SIGCONT}}){auto action=menu.addAction(pair.first);action->setEnabled(p.pid>1&&p.pid!=getpid());connect(action,&QAction::triggered,this,[this,pair]{signalSelected(pair.second);});}
 menu.addSeparator();auto priorities=menu.addMenu("Priorité");auto group=new QActionGroup(&menu);for(auto level:priorityLevels){auto action=priorities->addAction(level.first);action->setCheckable(true);action->setChecked(priorityName(p.nice)==level.first);group->addAction(action);connect(action,&QAction::triggered,this,[this,p,level]{applyPriority(p,level.second);});}
 menu.addSeparator();connect(menu.addAction("Détails"),&QAction::triggered,this,[this]{showDetails();});
 menu.exec(table->viewport()->mapToGlobal(pos));if(running)timer.start();
 }

 void showDetails(){Proc p;if(!target(p))return;QMessageBox box(this);box.setWindowTitle(T("Détails processus","Process details"));box.setTextFormat(Qt::PlainText);box.setText(p.name+"\nPID : "+QString::number(p.pid)+"\nUtilisateur : "+p.user+"\nMémoire : "+bytes(p.rss)+"\nCPU total : "+QString::number(p.cpu,'f',1)+" %\nPriorité : "+QString::number(p.nice));box.setDetailedText(p.command.isEmpty()?T("Commande indisponible","Command unavailable"):p.command);box.exec();}
 void exportProcessesCsv(){QString path=QFileDialog::getSaveFileName(this,T("Exporter les processus","Export processes"),QDir::homePath()+"/pulse-processes.csv","CSV (*.csv)");if(path.isEmpty())return;QFile f(path);if(!f.open(QIODevice::WriteOnly|QIODevice::Text)){QMessageBox::warning(this,T("Export impossible","Export failed"),f.errorString());return;}QTextStream out(&f);out<<"name,pid,cpu,memory,user,state,priority\n";for(auto it=rows.cbegin();it!=rows.cend();++it){const auto&p=it.value();QStringList cols{p.name,QString::number(p.pid),QString::number(p.cpu,'f',1),QString::number(p.rss),p.user,p.state,QString::number(p.nice)};for(auto &v:cols){v.replace(QChar(34),QString("\"\""));v=QString("\"")+v+QString("\"");}out<<cols.join(',')<<"\n";}statusBar()->showMessage(T("Export CSV terminé","CSV export completed"),4000);}
 void closeEvent(QCloseEvent *e)override{settings.setValue("geometry",saveGeometry());QMainWindow::closeEvent(e);}
};

int main(int argc,char **argv){
 QApplication app(argc,argv);app.setApplicationName("Pulse");app.setDesktopFileName("pulse");app.setStyle("Fusion");QIcon::setFallbackThemeName("hicolor");if(QIcon::themeName().isEmpty())QIcon::setThemeName("breeze");
 if(app.arguments().contains("--self-test")){
 Monitor m;m.sample();QThread::msleep(200);auto s=m.sample();bool found=false;for(auto p:s.processes)if(p.pid==getpid())found=true;
 bool ok=s.total>0&&s.available<=s.total&&found&&s.cpu>=0&&s.cpu<=100;
 QProcess child;child.start("/usr/bin/sleep",{"30"});ok=child.waitForStarted()&&ok;
 if(child.state()==QProcess::Running){int pid=child.processId();Proc p;ok=readProc(pid,p)&&p.name=="sleep"&&ok;
 int fd=syscall(SYS_pidfd_open,pid,0);ok=fd>=0&&ok;
 if(fd>=0){ok=syscall(SYS_pidfd_send_signal,fd,SIGSTOP,nullptr,0)==0&&ok;QThread::msleep(50);Proc stopped;ok=readProc(pid,stopped)&&stopped.state=="T"&&ok;
 ok=syscall(SYS_pidfd_send_signal,fd,SIGCONT,nullptr,0)==0&&ok;ok=setpriority(PRIO_PROCESS,pid,10)==0&&ok;
 Proc changed;ok=readProc(pid,changed)&&changed.nice==10&&ok;ok=syscall(SYS_pidfd_send_signal,fd,SIGTERM,nullptr,0)==0&&ok;::close(fd);}
 if(!child.waitForFinished(2000)){child.kill();child.waitForFinished();ok=false;}}
 printf("procfs + owned child pause/resume/priority/terminate: %s | processes=%lld memory=%llu cpu=%.2f\n",ok?"PASS":"FAIL",(long long)s.processes.size(),(unsigned long long)s.total,s.cpu);return ok?0:1;}
 Window w;w.show();
 if(app.arguments().contains("--ui-test")){QTimer::singleShot(1500,&app,[&]{
 auto pages=w.findChild<QStackedWidget*>();auto tree=w.findChild<QTreeWidget*>();auto search=w.findChild<QLineEdit*>();
 // Count leaf items (process rows)
 int leafCount=0;
 if(tree){for(int i=0;i<tree->topLevelItemCount();++i){auto cat=tree->topLevelItem(i);leafCount+=cat->childCount();}}
 bool ok=pages&&tree&&search&&leafCount>0;
 if(ok){pages->setCurrentIndex(1);search->setText("__pulse_no_such_process__");
 int filtered=0;if(tree){for(int i=0;i<tree->topLevelItemCount();++i){auto cat=tree->topLevelItem(i);filtered+=cat->childCount();}}
 ok=filtered==0&&ok;search->clear();
 int restored=0;if(tree){for(int i=0;i<tree->topLevelItemCount();++i){auto cat=tree->topLevelItem(i);restored+=cat->childCount();}}
 ok=restored>0&&ok;
 // Sort by memory (col 3) descending
 tree->sortItems(3,Qt::DescendingOrder);
 // Check all visible leaf items have icons
 for(int i=0;i<tree->topLevelItemCount();++i){auto cat=tree->topLevelItem(i);for(int j=0;j<cat->childCount();++j)ok=!cat->child(j)->icon(0).isNull()&&ok;}
 app.processEvents();w.grab().save("pulse-processes.png");
 // Select first leaf item
 for(int i=0;i<tree->topLevelItemCount();++i){auto cat=tree->topLevelItem(i);if(cat->childCount()>0){tree->setCurrentItem(cat->child(0));break;}}
 QTimer::singleShot(100,&app,[&]{auto menu=qobject_cast<QMenu*>(app.activePopupWidget());if(!menu){ok=false;return;}QMenu *priorities=nullptr;QStringList actions;for(auto action:menu->actions()){actions<<action->text();if(action->menu())priorities=action->menu();}bool force=actions.contains("Force stop")||actions.contains("Forcer l’arrêt")||actions.contains("Forcer l'arrêt");bool suspend=actions.contains("Suspend")||actions.contains("Suspendre");bool resume=actions.contains("Resume")||actions.contains("Reprendre");ok=force&&suspend&&resume&&priorities&&ok;if(priorities){QStringList labels;for(auto action:priorities->actions())labels<<action->text();ok=(labels==QStringList({"Très basse","Basse","Normale","Haute","Très haute"})||labels==QStringList({"Very low","Low","Normal","High","Very high"}))&&ok;priorities->grab().save("pulse-priorities.png");}menu->grab().save("pulse-context-menu.png");menu->close();});
 // Trigger context menu on first leaf item
 for(int i=0;i<tree->topLevelItemCount();++i){auto cat=tree->topLevelItem(i);if(cat->childCount()>0){tree->customContextMenuRequested(tree->visualItemRect(cat->child(0)).center());break;}}
 pages->setCurrentIndex(2);app.processEvents();w.grab().save("pulse-settings.png");}
 printf("UI navigation/search/numeric sort/icons/context menu/priority labels: %s\n",ok?"PASS":"FAIL");app.exit(ok?0:1);});}
 if(app.arguments().contains("--screenshot")){QTimer::singleShot(2500,&app,[&]{w.findChild<QStackedWidget*>()->setCurrentIndex(0);app.processEvents();w.grab().save("pulse-preview.png");app.quit();});}
 return app.exec();
}
