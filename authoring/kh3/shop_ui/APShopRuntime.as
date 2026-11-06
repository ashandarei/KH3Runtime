package kh3ap
{
   import flash.events.Event;
   import flash.geom.ColorTransform;
   import flash.text.TextField;
   import flash.text.TextFormat;
   import flash.utils.getDefinitionByName;

   // Original AP extension. Stock classes, timeline scripts and menu setup
   // stay in the game's movie; only their constructor call sites are extended.
   public final class APShopRuntime
   {
      private static const STATE:String = "kh3apShopRowState";
      private static const ICON_ROOT:String = "/Game/UI/Icon/Item/";
      private static const USEFUL_COLOR:uint = 0x8FD8FF;
      private static const PROGRESSION_COLOR:uint = 0xFFF0A0;

      public static function attachRow(row:Object) : void
      {
         if(row[STATE] != null)
         {
            return;
         }
         row[STATE] = {icons:{}, icon:null, path:"", colorCaptured:false, defaultColor:0};
         row.addEventListener(Event.ENTER_FRAME,refreshRow,false,0,true);
      }

      public static function attachForge(menu:Object) : void
      {
         menu.cmd2.enabled = false;
         menu.cmd2.visible = false;
         menu.cmd1.toDown = 33;
         menu.cmd3.toUp = 31;
      }

      private static function refreshRow(event:Event) : void
      {
         var row:Object = event.currentTarget;
         var state:Object = row[STATE];
         var label:Object = row["itemLabel"];
         if(state == null || label == null)
         {
            return;
         }
         var wrapper:Object = label["txName"];
         var category:TextField = label["txIcon"] as TextField;
         if(wrapper == null || category == null)
         {
            return;
         }
         // Preserve markers in the wrapper so recycled rows keep their identity.
         // Strip them only from the nested field which the movie renders.
         var markedName:String = wrapper.text;
         var marker:int = markedName != null && markedName.length > 0 ? int(markedName.charCodeAt(0)) : 0;
         var isAP:Boolean = marker == 8290 || marker == 8291 || marker == 8292;
         var gameMarker:int = isAP && markedName.length > 1 ? int(markedName.charCodeAt(1)) : 0;
         var iconName:String = "";
         switch(gameMarker)
         {
            case 8293: iconName = "ap_kh1"; break;
            case 8294: iconName = "ap_kh2"; break;
            case 8295: iconName = "ap_kh_bbs"; break;
            case 8296: iconName = "ap_kh_com"; break;
            case 8297: iconName = "ap_kh_ddd"; break;
         }
         var hasGameIcon:Boolean = iconName.length > 0;
         var name:String = isAP ? markedName.substring(hasGameIcon ? 2 : 1) : markedName;
         if(!hasGameIcon)
         {
            if(name == "Ultima Weapon Shard")
            {
               iconName = "ap_ultima_weapon_shop_row";
            }
            else if(isAP)
            {
               iconName = marker == 8292 ? "ap_archipelago_progression" :
                          marker == 8290 ? "ap_archipelago_useful" : "ap_archipelago";
            }
         }
         var field:TextField = wrapper.textField as TextField;
         if(field != null)
         {
            if(!state.colorCaptured)
            {
               state.defaultColor = field.textColor;
               state.colorCaptured = true;
            }
            if(isAP)
            {
               field.text = name;
            }
            var color:uint = marker == 8292 ? PROGRESSION_COLOR :
                             marker == 8290 ? USEFUL_COLOR : uint(state.defaultColor);
            var format:TextFormat = field.defaultTextFormat;
            format.color = color;
            field.defaultTextFormat = format;
            field.textColor = color;
         }
         var custom:Boolean = iconName.length > 0;
         category.visible = !custom;
         if(state.icon != null)
         {
            state.icon.visible = custom;
         }
         if(!custom)
         {
            return;
         }
         var path:String = ICON_ROOT + iconName + "." + iconName;
         if(state.path != path)
         {
            if(state.icon != null)
            {
               state.icon.visible = false;
            }
            state.path = path;
            state.icon = state.icons[iconName];
            if(state.icon == null)
            {
               // Resolve the game's class at runtime, without compiling or
               // bundling its implementation or replacing a vanilla symbol.
               var IconClass:Class = getDefinitionByName("tres.controls.Icon") as Class;
               state.icon = new IconClass(32,32);
               state.icons[iconName] = state.icon;
               state.icon.x = -40;
               state.icon.y = 0;
               state.icon.transform.colorTransform = new ColorTransform(1,1,1,2,0,0,0,0);
               label.addChild(state.icon);
               // One request per path avoids stale asynchronous completions
               // when the game recycles a row for another item.
               state.icon.load(path);
            }
         }
         state.icon.visible = true;
      }
   }
}
