package kh3ap
{
   import flash.display.MovieClip;
   import flash.display.Sprite;
   import flash.events.Event;
   import flash.geom.Matrix;
   import flash.geom.Rectangle;
   import flash.text.TextField;
   import flash.text.TextFormat;
   import flash.utils.getTimer;

   // Independent music movie, with its own document class/application domain.
   public dynamic class APNowPlaying extends MovieClip
   {
      public var songTitle:String = "";
      public var active:Boolean = false;
      public var maxWords:int = 6;
      public var ready:int = 0;
      private var panel:Sprite;
      private var clip:Sprite;
      private var label:TextField;
      private var previous:String = "";
      private var previousWords:int = 0;
      private var started:int = 0;
      private var overflow:Number = 0;
      private var panelWidth:Number = 0;
      private static const HOLD_MS:Number = 2500;
      private static const SPEED:Number = 28;
      private static const SCALE:Number = 1;
      private static const WINDOW_SCALE:Number = 1.5;
      private static const BACKGROUND_OPACITY:Number = 0.175;

      // Read-only bridge evidence; normal playback does not poll these.
      public function get scrollOffset() : Number { return label == null ? 0 : label.x; }
      public function get scrollOverflow() : Number { return overflow; }
      public function get panelVisible() : Number { return panel != null && panel.visible ? 1 : 0; }
      public function get renderedTitle() : String { return label == null ? "" : label.text; }
      public function get titleWindowWidth() : Number { return clip == null || clip.scrollRect == null ? 0 : clip.scrollRect.width; }
      public function get displayScale() : Number { return SCALE; }
      public function get windowScale() : Number { return WINDOW_SCALE; }
      public function get backgroundOpacity() : Number { return BACKGROUND_OPACITY; }
      public function get panelX() : Number { return panel == null ? 0 : panel.x; }
      public function get panelY() : Number { return panel == null ? 0 : panel.y; }
      public function get viewportWidth() : Number { return stage == null ? 0 : stage.stageWidth; }
      public function get viewportHeight() : Number { return stage == null ? 0 : stage.stageHeight; }

      public function APNowPlaying()
      {
         super();
         mouseEnabled = false;
         mouseChildren = false;
         panel = new Sprite();
         panel.scaleX = SCALE;
         panel.scaleY = SCALE;
         addChild(panel);
         clip = new Sprite();
         clip.y = 7;
         clip.x = 12;
         panel.addChild(clip);
         label = new TextField();
         label.defaultTextFormat = new TextFormat("$sougei",20,0xFFFFFF);
         label.selectable = false;
         label.multiline = false;
         label.wordWrap = false;
         label.autoSize = "left";
         clip.addChild(label);
         panel.visible = false;
         addEventListener(Event.ENTER_FRAME,refresh,false,0,true);
         ready = 1;
      }

      private function refresh(event:Event) : void
      {
         if(stage == null) return;
         panel.visible = active && songTitle.length > 0;
         if(!panel.visible) return;
         var limit:int = Math.max(1,Math.min(12,maxWords));
         // Bound by both a word count and screen space: even a single long
         // filename must scroll rather than overlap the rest of the HUD.
         var available:Number = Math.min(420,stage.stageWidth * 0.32) * WINDOW_SCALE;
         if(previous != songTitle || previousWords != limit || panelWidth != available)
         {
            previous = songTitle;
            previousWords = limit;
            panelWidth = available;
            // KH3's shipping Scaleform crashes on RegExp-based String.split.
            var words:Array = songTitle.split(" ");
            label.text = words.slice(0,limit).join(" ");
            var width:Number = Math.min(available,Math.max(100,label.textWidth + 6) * WINDOW_SCALE);
            label.text = songTitle;
            overflow = Math.max(0,label.textWidth + 6 - width);
            clip.scrollRect = new Rectangle(0,0,width,30);
            var backgroundWidth:Number = width + 24;
            var fadeRatio:Number = Math.min(64,backgroundWidth * 0.20) / backgroundWidth * 255;
            var gradient:Matrix = new Matrix();
            gradient.createGradientBox(backgroundWidth,44,0,0,0);
            panel.graphics.clear();
            panel.graphics.beginGradientFill("linear",[0x101820,0x101820,0x101820,0x101820],
               [0,BACKGROUND_OPACITY,BACKGROUND_OPACITY,0],[0,fadeRatio,255 - fadeRatio,255],gradient);
            panel.graphics.drawRoundRect(0,0,backgroundWidth,44,10,10);
            panel.graphics.endFill();
            started = getTimer();
         }
         var displayWidth:Number = (clip.scrollRect.width + 24) * SCALE;
         panel.x = Math.max(12,(stage.stageWidth - displayWidth) / 2);
         panel.y = 2;
         var travel:Number = overflow / SPEED * 1000;
         var elapsed:Number = (getTimer() - started) % (HOLD_MS * 2 + travel);
         label.x = overflow > 0 ? -Math.min(overflow,Math.max(0,elapsed - HOLD_MS) * SPEED / 1000) : (clip.scrollRect.width - label.textWidth - 6) / 2;
      }
   }
}
