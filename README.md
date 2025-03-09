<div align="center" style="margin-top: 10px;">
<a href="https://github.com/murdoockds/clip2disc"><img src="https://github.com/murdoockds/clip2disc/raw/main/resources/images/clip2disc.png" title="Logo" style="max-width:100%;" width="128" /></a>
</div>
<div align="center">

</div></h1>

Clip2Disc is a application that receives a video file as input, trims it to keep only the last 30 seconds and compresses it to under 10MB, which is the maximum file size a non-nitro Discord user can send in chats.

The application works as a wrapper for FFmpeg, that is the true compressor app behind the scenes.

It was built using C++ on Qt Creator 15.0.1.

> As the app always compress the bitrate and resolution, it's recommended that the input video is in, at least, Full HD resolution. Otherwise the result video will have a awful quality.
