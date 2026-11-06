package kh3ap
{
   import flash.display.MovieClip;
   import flash.display.Bitmap;
   import flash.display.Loader;
   import flash.display.Sprite;
   import flash.events.Event;
   import flash.events.MouseEvent;
   import flash.events.IOErrorEvent;
   import flash.geom.ColorTransform;
   import flash.geom.Rectangle;
   import flash.text.TextField;
   import flash.text.TextFormat;
   import flash.net.URLRequest;
   import flash.utils.getDefinitionByName;

   // Original UI only. The game supplies its existing Icon class and textures.
   public dynamic class APIngredientJournal extends MovieClip
   {
      public var cursorSoundSerial:int = 0;
      public var active:Boolean = false;
      public var ready:Boolean = false;
      public var selectedIngredient:String = "";
      public var selectedWorld:String = "All Ingredients";
      public var paneMode:int = 0;
      public var mapRequestKey:String = "";
      public var mapRequestSerial:int = 0;
      public var mapState:String = "closed";
      public var mapTexture:String = "";
      public var mapMarkerCount:int = 0;
      public var mapPageCount:int = 0;
      private var host:Object;
      private var page:Sprite;
      private var grid:Sprite;
      private var detail:Sprite;
      private var sourceContent:Sprite;
      private var sourceDescription:String = "";
      private var sourceScroll:Number = 0;
      private var sourceRows:Array = [];
      private var selectedSource:int = 0;
      private var sourceIngredient:String = "";
      private var mapPage:Sprite;
      private var mapCanvas:Sprite;
      private var mapHeading:TextField;
      private var mapCaption:TextField;
      private var mapStatus:TextField;
      private var mapLoader:Loader;
      private var mapSheets:Array = [];
      private var mapIndex:int = 0;
      private var progress:TextField;
      private var worldLabel:TextField;
      private var entries:Array = [];
      private var worlds:Array = ["All Ingredients"];
      private var visibleEntries:Array = [];
      private var cells:Array = [];
      private var collected:Object = {};
      private var selected:int = 0;
      private var worldIndex:int = 0;
      private var focused:Boolean = false;
      public var adversaries:Boolean = false;
      public var gates:Boolean = false;
      public var gateRequest:int = 0;
      public var gateCount:int = 0;
      private var gatePage:Sprite;
      private var gateWorlds:Array = [];
      private var gateIcons:Object = {};
      private var gateSelected:int = 0;
      private var gateMessage:String = "";
      private var gatePending:Boolean = false;
      public var gateYes:Boolean = true;
      private var gridScroll:Number = 0;
      private function get columns() : int { return adversaries ? 4 : COLUMNS; }
      private var font:String = "$sougei";
      private static const COLUMNS:int = 8;

      public function get mode() : int { return active ? 1 : 0; }
      public function get catalogCount() : int { return ready ? entries.length : 0; }
      public function get entryVisible() : int { return (gates ? gatePage.visible : page.visible) ? 1 : 0; }
      public function get detailText() : String { return sourceDescription; }
      public function get detailLineCount() : int { return sourceContent.numChildren; }
      public function get selectedLocation() : String
      {
         return visibleEntries.length > selected && visibleEntries[selected].sources.length > selectedSource ? visibleEntries[selected].sources[selectedSource].map : "";
      }
      public function get loadedIconCount() : int
      {
         var count:int = 0;
         for each(var cell:Object in cells) if(cell.foodIcon.width > 1 && cell.foodIcon.height > 1) count++;
         return count;
      }
      public function get selectedIconAlpha() : Number { return cells.length > selected ? cells[selected].foodIcon.alpha : 0; }

      public static function attach(root:Object) : void
      {
         if(root.getChildByName("kh3apIngredients") == null)
         {
            try
            {
               var journal:APIngredientJournal = new APIngredientJournal(root);
               journal.name = "kh3apIngredients";
               root.addChild(journal);
            }
            catch(error:Error)
            {
               var failure:MovieClip = new MovieClip();
               failure.name = "kh3apIngredientsError";
               failure["failure"] = error.toString();
               root.addChild(failure);
            }
         }
      }

      public function APIngredientJournal(root:Object)
      {
         super();
         host = root;
         page = new Sprite();
         page.visible = false;
         page.x = 96;
         page.y = 148;
         // Leave the game's existing header exposed; draw only the body panes.
         page.graphics.beginFill(0x0A0D12);
         page.graphics.drawRect(0,0,1728,904);
         page.graphics.endFill();
         page.graphics.beginFill(0x193332);
         page.graphics.drawRect(896,0,832,904);
         page.graphics.endFill();
         page.graphics.lineStyle(2,0x667784);
         page.graphics.drawRect(0,0,1728,904);
         page.graphics.moveTo(896,0);
         page.graphics.lineTo(896,904);
         addChild(page);
         gatePage = new Sprite();
         gatePage.x = 96;
         gatePage.y = 148;
         gatePage.visible = false;
         addChild(gatePage);
         progress = text("",36,19,818,48,28,0xFFFFFF);
         page.addChild(progress);
         var previous:Sprite = button("<",28,91,48,38);
         var next:Sprite = button(">",810,91,48,38);
         previous.addEventListener(MouseEvent.CLICK,onPreviousWorld);
         next.addEventListener(MouseEvent.CLICK,onNextWorld);
         page.addChild(previous);
         page.addChild(next);
         page.addChild(text("L1",33,70,48,26,16,0xFFFFFF));
         page.addChild(text("R1",815,70,48,26,16,0xFFFFFF));
         worldLabel = text("All Ingredients",88,91,706,40,24,0xFFFFFF);
         page.addChild(worldLabel);
         grid = new Sprite();
         grid.x = 36;
         grid.y = 160;
         grid.scrollRect = new Rectangle(0,0,824,680);
         grid.addEventListener(MouseEvent.MOUSE_WHEEL,onGridWheel);
         page.addChild(grid);
         page.addChild(text("Where to find it",928,19,764,40,26,0xFFFFFF));
         detail = new Sprite();
         detail.x = 928;
         detail.y = 85;
         detail.scrollRect = new Rectangle(0,0,764,779);
         sourceContent = new Sprite();
         detail.addChild(sourceContent);
         detail.addEventListener(MouseEvent.MOUSE_WHEEL,onDetailWheel);
         page.addChild(detail);
         mapPage = new Sprite();
         mapPage.graphics.beginFill(0x0A0D12);
         mapPage.graphics.drawRect(0,0,1728,904);
         mapPage.graphics.endFill();
         mapPage.visible = false;
         page.addChild(mapPage);
         mapHeading = text("",44,18,1640,44,28,0xFFFFFF);
         mapCaption = text("",44,66,1640,36,22,0xA9DCC8);
         mapStatus = text("",44,850,1640,38,22,0xFFFFFF);
         mapPage.addChild(mapHeading);
         mapPage.addChild(mapCaption);
         mapCanvas = new Sprite();
         mapPage.addChild(mapCanvas);
         mapPage.addChild(mapStatus);
         host.addEventListener("tresFocusIn",onFocusIn,false,0,true);
         host.addEventListener("tresFocusOut",onFocusOut,false,0,true);
         host.addEventListener(Event.REMOVED_FROM_STAGE,onFocusOut,false,0,true);
      }

      private function text(value:String,xpos:Number,ypos:Number,w:Number,h:Number,size:int,color:uint) : TextField
      {
         var field:TextField = new TextField();
         field.defaultTextFormat = new TextFormat(font,size,color);
         field.text = value;
         field.x = xpos;
         field.y = ypos;
         field.width = w;
         field.height = h;
         field.selectable = false;
         field.mouseEnabled = false;
         return field;
      }

      private function button(label:String,xpos:Number,ypos:Number,w:Number,h:Number) : Sprite
      {
         var result:Sprite = new Sprite();
         result.x = xpos;
         result.y = ypos;
         result.graphics.lineStyle(1,0x58BBCD);
         result.graphics.beginFill(0x153B50);
         result.graphics.drawRoundRect(0,0,w,h,8);
         result.graphics.endFill();
         result.buttonMode = true;
         result.addChild(text(label,8,2,w-16,h-4,22,0xFFFFFF));
         return result;
      }

      private function gateLabel(value:String,xpos:Number,w:Number,size:int,color:uint) : TextField
      {
         var field:TextField = text(value,xpos,0,w,62,size,color);
         // TextField adds a two-pixel inset above the measured text.
         field.y = (62-field.textHeight)/2-2;
         return field;
      }

      public function loadCatalog(value:String) : Object
      {
         if(ready) return this;
         var lines:Array = value.split("\n");
         for each(var line:String in lines)
         {
            if(line.length == 0) continue;
            var columns:Array = line.split("\t");
            if(columns.length != 4) return null;
            var places:Array = [];
            for each(var source:String in String(columns[3]).split("|"))
            {
               if(source.length == 0) continue;
               var pair:Array = source.split("~");
               if(pair.length != 3) return null;
               places.push({world:pair[0],map:pair[1],key:pair[2]});
               if(worlds.indexOf(pair[0]) < 0) worlds.push(pair[0]);
            }
            entries.push({id:columns[0],name:columns[1],icon:columns[2],sources:places});
         }
         if(entries.length != (adversaries ? 53 : 51)) return null;
         worlds = [adversaries ? "All Adversaries" : "All Ingredients"].concat(worlds.slice(1).sort());
         ready = true;
         rebuild();
         return this;
      }

      public function setCollected(value:String) : Object
      {
         collected = {};
         for each(var id:String in value.split(",")) if(id.length > 0) collected[id] = true;
         paint();
         return this;
      }

      // The native string bridge rejects zero-length arguments. A fresh run
      // uses this no-argument call to publish its empty collection reliably.
      public function clearCollected() : Object { return setCollected(""); }

      public function setJournal(value:int) : void
      {
         setFocus(0);
         adversaries = value == 1;
         gates = value == 2;
         gateRequest = 0;
         gatePending = false;
         gateSelected = 0;
         ready = false;
         entries = [];
         collected = {};
         worlds = [adversaries ? "All Adversaries" : "All Ingredients"];
         worldIndex = selected = selectedSource = 0;
         gridScroll = 0;
         sourceIngredient = "";
         selectedWorld = worlds[0];
      }

      public function setFocus(value:int) : void
      {
         focused = value != 0;
         active = focused && ready;
         page.visible = active && !gates;
         gatePage.visible = active && gates;
         if(!active) { closeMap(); paneMode = 0; }
         if(active) { if(gates) paintGates(); else paint(); }
      }
      public function setGateState(value:String) : Object
      {
         var lines:Array = value.split("\n");
         var header:Array = String(lines.shift()).split("\t");
         var previousCount:int = gateCount;
         gateCount = int(header[0]);
         gateMessage = String(header[1]);
         gateWorlds = [];
         for each(var line:String in lines)
         {
            var fields:Array = line.split("\t");
            if(fields.length == 4) gateWorlds.push({id:int(fields[0]),name:fields[1],locked:fields[2]=="1",icon:fields[3]});
         }
         if(gatePending && gateCount != previousCount) { gatePending = false; paneMode = 0; }
         gateSelected = Math.max(0,Math.min(gateSelected,gateWorlds.length-1));
         ready = true;
         paintGates();
         return this;
      }
      public function gateRejected() : void
      {
         gatePending = false;
         paneMode = 0;
         paintGates();
      }
      private function paintGates() : void
      {
         var IconClass:Class = getDefinitionByName("tres.controls.Icon") as Class;
         while(gatePage.numChildren > 0) gatePage.removeChildAt(0);
         gatePage.graphics.clear();
         gatePage.graphics.beginFill(0x0A0D12);
         gatePage.graphics.drawRect(0,0,1728,904);
         gatePage.graphics.endFill();
         gatePage.addChild(text("Available: " + gateCount,1260,25,400,50,28,0xA9EAF2));
         gatePage.addChild(text(gatePending ? "Opening world..." : gateMessage,40,25,1180,80,28,0xC2CFD8));
         if(gateWorlds.length > 0 && !gateWorlds[gateSelected].locked) selectNextGate(1);
         for(var i:int=0;i<gateWorlds.length;i++)
         {
            var row:MovieClip = new MovieClip();
            row.x = 40 + int(i/6)*830;
            row.y = 118 + (i%6)*78;
            row["gateIndex"] = i;
            row.buttonMode = gateWorlds[i].locked && gateCount > 0;
            row.graphics.lineStyle(2,i==gateSelected ? 0xA9EAF2 : 0x425968);
            row.graphics.beginFill(i==gateSelected ? 0x245870 : 0x10212D);
            row.graphics.drawRoundRect(0,0,790,62,8);
            row.graphics.endFill();
            var icon:Object=gateIcons[gateWorlds[i].id];
            if(icon == null) {
               icon=new IconClass(72,56);
               icon.load(gateWorlds[i].icon,true);
               icon.mouseEnabled=false; icon.mouseChildren=false;
               gateIcons[gateWorlds[i].id]=icon;
            }
            icon.x=12; icon.y=3;
            row.addChild(icon);
            row.addChild(gateLabel(gateWorlds[i].name,100,510,28,gateWorlds[i].locked ? 0xFFFFFF : 0x89929A));
            if(!gateWorlds[i].locked) {
               row.alpha = 0.45;
               row.addChild(gateLabel("Unlocked",610,165,22,0xA8B0B8));
            }
            row.addEventListener(MouseEvent.CLICK,onGateClick);
            gatePage.addChild(row);
         }
         if(paneMode == 1 && !gatePending)
         {
            var shade:Sprite = new Sprite();
            shade.graphics.beginFill(0x000000,0.75);
            shade.graphics.drawRect(0,0,1728,904);
            shade.graphics.endFill();
            gatePage.addChild(shade);
            var modal:Sprite = new Sprite();
            modal.x=364; modal.y=240;
            modal.graphics.lineStyle(2,0xA9EAF2);
            modal.graphics.beginFill(0x10212D);
            modal.graphics.drawRoundRect(0,0,1000,360,14);
            modal.graphics.endFill();
            modal.addChild(text("Are you sure?",40,28,920,60,38,0xFFFFFF));
            modal.addChild(text("Permanently unlock " + gateWorlds[gateSelected].name + "?",40,110,920,50,28,0xC2CFD8));
            modal.addChild(text("This uses one Gate Opener.",40,170,920,50,28,0xC2CFD8));
            var yes:Sprite = button((gateYes ? "> " : "") + "Yes",160,270,300,58);
            var no:Sprite = button((!gateYes ? "> " : "") + "No",540,270,300,58);
            yes.alpha=gateYes ? 1 : 0.5;
            no.alpha=gateYes ? 0.5 : 1;
            yes.addEventListener(MouseEvent.CLICK,onGateConfirm);
            no.addEventListener(MouseEvent.CLICK,onGateCancel);
            modal.addChild(yes); modal.addChild(no);
            gatePage.addChild(modal);
         }
      }
      private function onGateClick(event:MouseEvent) : void
      {
         if(gatePending || paneMode != 0 || !gateWorlds[int(event.currentTarget["gateIndex"])].locked) return;
         gateSelected = int(event.currentTarget["gateIndex"]);
         paneMode = gateCount > 0 ? 1 : 0;
         gateYes = true;
         paintGates();
         event.stopImmediatePropagation();
      }
      private function onGateConfirm(event:MouseEvent) : void { gateYes=true; navigate(11); event.stopImmediatePropagation(); }
      private function onGateCancel(event:MouseEvent) : void { navigate(7); event.stopImmediatePropagation(); }
      private function selectNextGate(step:int) : void
      {
         for(var n:int=0;n<gateWorlds.length;n++) {
            gateSelected=(gateSelected+gateWorlds.length+step)%gateWorlds.length;
            if(gateWorlds[gateSelected].locked) return;
         }
      }
      private function navigateGates(command:int) : void
      {
         if(gatePending) return;
         if(command == 7) paneMode = 0;
         else if(command == 11 && gateCount > 0 && gateWorlds.length > 0 && gateWorlds[gateSelected].locked)
         {
            if(paneMode == 0) { paneMode = 1; gateYes = true; }
            else if(gateYes) { gateRequest = gateWorlds[gateSelected].id; gatePending = true; }
            else paneMode = 0;
         }
         else if(command >= 1 && command <= 4 && gateWorlds.length > 0)
         {
            if(paneMode == 1) { gateYes = !gateYes; cursorSoundSerial++; }
            else {
               var previous:int = gateSelected;
               if(command <= 2) gateSelected=(gateSelected+6)%gateWorlds.length;
               if(command > 2 || !gateWorlds[gateSelected].locked) selectNextGate(command==1 || command==3 ? -1 : 1);
               if(previous != gateSelected) cursorSoundSerial++;
            }
         }
         paintGates();
      }
      private function onFocusIn(event:Event) : void { setFocus(1); }
      private function onFocusOut(event:Event) : void { if(event.target == host) setFocus(0); }
      private function onPreviousWorld(event:MouseEvent) : void { navigate(5); event.stopImmediatePropagation(); }
      private function onNextWorld(event:MouseEvent) : void { navigate(6); event.stopImmediatePropagation(); }
      private function onDetailWheel(event:MouseEvent) : void { scrollSources(-event.delta * 30); event.stopImmediatePropagation(); }
      private function onGridWheel(event:MouseEvent) : void
      {
         if(paneMode != 0) return;
         gridScroll = Math.max(0,Math.min(Math.max(0,Math.ceil(cells.length/columns)*88-680),gridScroll-event.delta*44));
         grid.scrollRect = new Rectangle(0,gridScroll,824,680);
         event.stopImmediatePropagation();
      }

      private function scrollSources(step:Number) : void
      {
         sourceScroll = Math.max(0,Math.min(Math.max(0,sourceContent.height - 779),sourceScroll + step));
         sourceContent.y = -sourceScroll;
      }

      private function showSources(ingredient:Object) : void
      {
         if(sourceIngredient != ingredient.id) { selectedSource = 0; sourceScroll = 0; }
         sourceIngredient = ingredient.id;
         while(sourceContent.numChildren > 0) sourceContent.removeChildAt(0);
         sourceRows = [];
         sourceContent.addChild(text(ingredient.name,0,0,740,44,28,0xFFFFFF));
         var status:String = collected[ingredient.id] === true ? (adversaries ? "Defeated" : "Collected") : (adversaries ? "Not defeated yet" : "Not collected yet");
         sourceContent.addChild(text(status,0,46,740,36,22,collected[ingredient.id] === true ? 0x56DDA9 : 0x9CA8B2));
         sourceDescription = ingredient.name + " | " + status;
         var world:String = "";
         var ypos:Number = 108;
         if(ingredient.sources.length == 0)
            sourceContent.addChild(text("No verified source map is available yet.",0,ypos,740,80,22,0x9CA8B2));
         for each(var place:Object in ingredient.sources)
         {
            if(place.world != world)
            {
               if(world.length > 0) ypos += 24;
               world = place.world;
               sourceContent.addChild(text(world,0,ypos,740,40,24,0xA9DCC8));
               sourceDescription += " | " + world + ":";
               ypos += 43;
            }
            if(place.key.length == 0)
            {
               sourceContent.addChild(text(place.map + " (no map available)",24,ypos,716,36,22,0xFFFFFF));
               sourceDescription += " " + place.map + " (no map available);";
               ypos += 36;
               continue;
            }
            var row:MovieClip = new MovieClip();
            row.y = ypos;
            row["sourceIndex"] = sourceRows.length;
            row["placeIndex"] = ingredient.sources.indexOf(place);
            row.buttonMode = true;
            row.addChild(text(place.map,24,0,716,36,22,0xFFFFFF));
            row.addEventListener(MouseEvent.ROLL_OVER,onSourceHover);
            row.addEventListener(MouseEvent.CLICK,onSourceClick);
            sourceRows.push(row);
            sourceContent.addChild(row);
            sourceDescription += " " + place.map + ";";
            ypos += 36;
         }
         paintSources();
      }

      private function paintSources() : void
      {
         for(var i:int=0;i<sourceRows.length;i++)
         {
            var row:Sprite = sourceRows[i];
            row.graphics.clear();
            if(paneMode == 1 && i == selectedSource)
            {
               row.graphics.lineStyle(2,0xA9EAF2);
               row.graphics.beginFill(0x245870);
               row.graphics.drawRect(0,0,740,36);
               row.graphics.endFill();
            }
         }
         if(paneMode == 1 && sourceRows.length > selectedSource)
         {
            var ypos:Number = sourceRows[selectedSource].y;
            if(ypos < sourceScroll) sourceScroll = ypos;
            if(ypos + 36 > sourceScroll + 779) sourceScroll = ypos + 36 - 779;
         }
         scrollSources(0);
      }
      private function onSourceHover(event:MouseEvent) : void
      {
         if(paneMode == 2) return;
         var before:String = cursorSelection();
         var pane:int = paneMode;
         paneMode = 1;
         selectedSource = int(event.currentTarget["sourceIndex"]);
         paint();
         cursorFeedback(before,pane);
         event.stopImmediatePropagation();
      }
      private function onSourceClick(event:MouseEvent) : void { onSourceHover(event); openMap(); }

      private function openMap() : void
      {
         if(sourceRows.length == 0) return;
         closeMap();
         var ingredient:Object = visibleEntries[selected];
         var place:Object = ingredient.sources[sourceRows[selectedSource]["placeIndex"]];
         paneMode = 2;
         mapRequestKey = place.key;
         mapRequestSerial++;
         mapState = "loading";
         mapHeading.text = ingredient.name;
         mapCaption.text = place.world + " / " + place.map;
         mapStatus.text = "Loading map...";
         mapPage.visible = true;
      }
      private function closeMap() : void
      {
         if(mapLoader != null)
         {
            mapLoader.contentLoaderInfo.removeEventListener(Event.COMPLETE,onMapLoaded);
            mapLoader.contentLoaderInfo.removeEventListener(IOErrorEvent.IO_ERROR,onMapError);
            try { mapLoader.close(); } catch(error:Error) {}
            mapLoader.unload();
            mapLoader = null;
         }
         while(mapCanvas.numChildren > 0)
         {
            var child:Object = mapCanvas.removeChildAt(0);
            if(child is MovieClip && child["foodIcon"] != null) child["foodIcon"].unload();
         }
         mapPage.visible = false;
         mapRequestKey = "";
         mapTexture = "";
         mapState = "closed";
         mapMarkerCount = 0;
         mapPageCount = 0;
         mapSheets = [];
      }
      // Native data and positions are resolved from installed assets in memory.
      // Each row is texture, friendly label, pixel width/height and marker XYs.
      public function loadMap(value:String) : Object
      {
         if(paneMode != 2) return null;
         mapSheets = [];
         for each(var line:String in value.split("\n"))
         {
            if(line.length == 0) continue;
            var columns:Array = line.split("\t");
            if(columns.length != 5) return null;
            mapSheets.push({texture:columns[0],label:columns[1],w:Number(columns[2]),
                           h:Number(columns[3]),points:String(columns[4]).split(";")});
         }
         mapPageCount = mapSheets.length;
         mapIndex = 0;
         showMapSheet();
         return this;
      }
      public function mapError(value:String) : Object
      {
         if(paneMode == 2) { mapState = "failed"; mapStatus.text = value; }
         return this;
      }
      private function showMapSheet() : void
      {
         if(mapLoader != null)
         {
            mapLoader.contentLoaderInfo.removeEventListener(Event.COMPLETE,onMapLoaded);
            mapLoader.contentLoaderInfo.removeEventListener(IOErrorEvent.IO_ERROR,onMapError);
            mapLoader.unload();
         }
         while(mapCanvas.numChildren > 0)
         {
            var child:Object = mapCanvas.removeChildAt(0);
            if(child is MovieClip && child["foodIcon"] != null) child["foodIcon"].unload();
         }
         if(mapSheets.length == 0) { mapError("No map is available for this location."); return; }
         var sheet:Object = mapSheets[mapIndex];
         mapTexture = sheet.texture;
         mapMarkerCount = 0;
         mapState = "loading";
         mapStatus.text = "Loading map...";
         mapCaption.text = sheet.label + (mapPageCount > 1 ? "     L1 / R1" : "");
         mapLoader = new Loader();
         mapLoader.contentLoaderInfo.addEventListener(Event.COMPLETE,onMapLoaded);
         mapLoader.contentLoaderInfo.addEventListener(IOErrorEvent.IO_ERROR,onMapError);
         mapLoader.load(new URLRequest("img:/" + mapTexture));
      }
      private function onMapError(event:IOErrorEvent) : void { mapError("This map could not be loaded."); }
      private function onMapLoaded(event:Event) : void
      {
         if(paneMode != 2 || event.target != mapLoader.contentLoaderInfo) return;
         var bitmap:Bitmap = mapLoader.content as Bitmap;
         if(bitmap == null) { mapError("This map could not be loaded."); return; }
         var sheet:Object = mapSheets[mapIndex];
         if(bitmap.width != sheet.w || bitmap.height != sheet.h) { mapError("Map dimensions could not be verified."); return; }
         var fit:Number = Math.min(1640/sheet.w,728/sheet.h);
         mapLoader.scaleX = mapLoader.scaleY = fit;
         mapLoader.x = (1728-sheet.w*fit)/2;
         mapLoader.y = 110+(728-sheet.h*fit)/2;
         mapCanvas.addChild(mapLoader);
         var IconClass:Class = getDefinitionByName("tres.controls.Icon") as Class;
         for each(var point:String in sheet.points)
         {
            var xy:Array = point.split(",");
            if(xy.length != 2) continue;
            var marker:MovieClip = new MovieClip();
            marker.x = mapLoader.x + Number(xy[0])*fit;
            marker.y = mapLoader.y + Number(xy[1])*fit;
            marker.graphics.lineStyle(3,0xFFE552);
            marker.graphics.beginFill(0x102633,0.95);
            marker.graphics.drawCircle(0,0,19);
            marker.graphics.endFill();
            if(!adversaries)
            {
               var icon:Object = new IconClass(28,28);
               icon.x = icon.y = -14;
               icon.load(visibleEntries[selected].icon,true);
               marker["foodIcon"] = icon;
               marker.addChild(icon);
            }
            mapCanvas.addChild(marker);
            mapMarkerCount++;
         }
         mapState = "ready";
         mapStatus.text = adversaries ? "Vanilla locations" : "Possible pickup locations: " + mapMarkerCount;
      }

      private function cursorSelection() : String
      {
         return worldIndex + ":" + selected + ":" + selectedSource + ":" + mapIndex;
      }
      private function cursorFeedback(before:String, pane:int) : void
      {
         if(active && pane == paneMode && before != cursorSelection()) cursorSoundSerial++;
      }
      public function navigate(command:int) : void
      {
         if(gates) { if(active) navigateGates(command); return; }
         var before:String = cursorSelection();
         var pane:int = paneMode;
         navigateInternal(command);
         cursorFeedback(before,pane);
      }
      private function navigateInternal(command:int) : void
      {
         if(!ready) return;
         if(command <= 0) return;
         if(!active) return;
         if(command == 7)
         {
            if(paneMode == 2) { closeMap(); paneMode = 1; paint(); }
            else if(paneMode == 1) { paneMode = 0; paint(); }
            return;
         }
         if(command == 11)
         {
            if(paneMode == 0 && sourceRows.length > 0) { paneMode = 1; paint(); }
            else if(paneMode == 1) openMap();
            return;
         }
         if(paneMode == 2)
         {
            if((command == 5 || command == 6) && mapSheets.length > 1)
            {
               mapIndex = (mapIndex + mapSheets.length + (command == 5 ? -1 : 1)) % mapSheets.length;
               showMapSheet();
            }
            return;
         }
         if(command == 8 || command == 9) { scrollSources(command == 8 ? -180 : 180); return; }
         if(command == 5 || command == 6)
         {
            worldIndex = (worldIndex + worlds.length + (command == 5 ? -1 : 1)) % worlds.length;
            selectedWorld = worlds[worldIndex];
            selected = 0;
            paneMode = 0;
            rebuild();
            return;
         }
         if(visibleEntries.length == 0) return;
         if(paneMode == 1)
         {
            if(command == 1) { paneMode = 0; paint(); return; }
            if((command == 3 || command == 4) && sourceRows.length > 0)
            {
               selectedSource = (selectedSource + sourceRows.length + (command == 3 ? -1 : 1)) % sourceRows.length;
               paintSources();
            }
            return;
         }
         var step:int = command == 1 ? -1 : command == 2 ? 1 : command == 3 ? -columns : columns;
         selected = (selected + visibleEntries.length + step) % visibleEntries.length;
         var ypos:Number = int(selected/columns)*88;
         if(ypos < gridScroll) gridScroll = ypos;
         if(ypos+80 > gridScroll+680) gridScroll = ypos+80-680;
         grid.scrollRect = new Rectangle(0,gridScroll,824,680);
         paint();
      }

      private function rebuild() : void
      {
         while(grid.numChildren > 0)
         {
            var old:Object = grid.removeChildAt(0);
            if(old["foodIcon"] != null && old["foodIcon"]["unload"] != null) old["foodIcon"].unload();
         }
         cells = [];
         gridScroll = 0;
         grid.scrollRect = new Rectangle(0,0,824,680);
         visibleEntries = [];
         for each(var entry:Object in entries)
         {
            var include:Boolean = worldIndex == 0;
            for each(var source:Object in entry.sources) if(source.world == selectedWorld) include = true;
            if(include) visibleEntries.push(entry);
         }
         var IconClass:Class = getDefinitionByName("tres.controls.Icon") as Class;
         for(var i:int=0;i<visibleEntries.length;i++)
         {
            var cell:MovieClip = new MovieClip();
            cell.x = (i % columns) * (adversaries ? 208 : 104);
            cell.y = int(i / columns) * 88;
            cell["entryIndex"] = i;
            cell.graphics.beginFill(0x153B50);
            cell.graphics.drawRect(0,0,adversaries ? 200 : 96,80);
            cell.graphics.endFill();
            var icon:Object = adversaries ? new MovieClip() : new IconClass(48,48);
            icon.x = 24;
            icon.y = 5;
            icon.mouseEnabled = false;
            icon.mouseChildren = false;
            // Native textures are preloaded and retained by the transient
            // movie. Bypass the stock root's external icon callback queue.
            if(!adversaries) icon.load(visibleEntries[i].icon,true);
            cell["foodIcon"] = icon;
            cell.addChild(icon);
            var label:TextField = adversaries ? text(visibleEntries[i].name,8,7,184,46,18,0xFFFFFF) : text(visibleEntries[i].name,3,57,90,21,14,0xFFFFFF);
            label.wordWrap = adversaries;
            if(adversaries)
            {
               var badge:TextField = text("",8,56,184,23,15,0xFFFFFF);
               cell["badge"] = badge;
               cell.addChild(badge);
            }
            cell["label"] = label;
            cell.addChild(label);
            cell.addEventListener(MouseEvent.ROLL_OVER,onHover);
            cell.addEventListener(MouseEvent.CLICK,onHover);
            cells.push(cell);
            grid.addChild(cell);
         }
         paint();
      }

      private function onHover(event:MouseEvent) : void
      {
         var before:String = cursorSelection();
         var pane:int = paneMode;
         selected = int(event.currentTarget["entryIndex"]);
         paneMode = 0;
         paint();
         if(event.type == MouseEvent.CLICK && sourceRows.length > 0) { paneMode = 1; paint(); }
         cursorFeedback(before,pane);
         event.stopImmediatePropagation();
      }

      private function paint() : void
      {
         if(!ready) return;
         var count:int = 0;
         for(var i:int=0;i<cells.length;i++)
         {
            var done:Boolean = collected[visibleEntries[i].id] === true;
            if(done) count++;
            var cell:Object = cells[i];
            cell.foodIcon.transform.colorTransform = done ? new ColorTransform() : new ColorTransform(0,0,0,1,145,145,145,0);
            cell.foodIcon.alpha = done ? 1 : 0.35;
            cell.label.textColor = done ? 0xFFFFFF : 0x7B8B96;
            cell.graphics.clear();
            cell.graphics.lineStyle(i == selected && paneMode == 0 ? 2 : 1,i == selected && paneMode == 0 ? 0xA9EAF2 : 0x244A60);
            cell.graphics.beginFill(i == selected ? 0x245870 : 0x153B50);
            cell.graphics.drawRect(0,0,adversaries ? 200 : 96,80);
            if(adversaries) { cell.badge.text = done ? "Defeated" : "Not defeated"; cell.badge.textColor = done ? 0x56DDA9 : 0x9CA8B2; }
            cell.graphics.endFill();
         }
         progress.text = (adversaries ? "Adversaries Defeated     " : "Ingredients Collected     ") + count + " / " + visibleEntries.length;
         worldLabel.text = selectedWorld;
         if(visibleEntries.length == 0) return;
         var ingredient:Object = visibleEntries[selected];
         selectedIngredient = ingredient.id;
         showSources(ingredient);
      }
   }
}
