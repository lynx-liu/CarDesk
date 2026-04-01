$(document).ready(function() {
	$(".suolue").stop().fadeTo(0,0.7);
	$(".suolue").stop().animate({"top":-30},1000);
	var key=0;
	var timer=setInterval(fun,5000);
	function fun(){

		key++;
		
		if(key>4)
		{
			key=0;
		}
		
		$(".main li").eq(key).stop().show().siblings().stop().hide();
		$(".suo li").eq(key).addClass('current').siblings().removeClass("current");
	};
	$(".suo li").click(function(event) {
		$(".main li").eq($(this).index()).stop().show().siblings().stop().hide();
		$(this).addClass('current').siblings().removeClass("current");
		key=$(this).index();
	});
	$("#box").hover(function() {
		clearInterval(timer);
	}, function() {
		timer=setInterval(fun,5000);
	
	});


});
