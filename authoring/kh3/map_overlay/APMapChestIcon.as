package
{
   import flash.display.MovieClip;

   // Frame 1 is the original chest; later frames are Keyblade tints.
   // Stop even unused hidden instances. Native C++ selects the palette frame
   // with gotoAndStop(), so chests do not animate or need a frame listener.
   public dynamic class APMapChestIcon extends MovieClip
   {
      public function APMapChestIcon()
      {
         super();
         this.stop();
      }
   }
}
