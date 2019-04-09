<template>
	<div ref='frame' class='scrollerFrame' @pointerdown='pointerdown' @pointermove='pointermove' @pointerup='pointerup'>
		<div ref='timeline' class='timeline'>
			<div class='dotFrame' :style='{left: dotCssLeft}' >
				<div class='dot'>
					<div class='dotdot' />
				</div>
			</div>
		</div>
	</div>
</template>

<script lang="ts">
import { Prop, Watch, Component, Vue } from 'vue-property-decorator';

@Component({})
export default class ImageScroller extends Vue {
	@Prop({ default: 0 }) pos!: number;

	dotWidth: number = 55; // must equal css style size
	isDragging: boolean = false;
	dragOffset: number = 0;

	get dotCssLeft(): string {
		return (this.pos * 100) + '%';
	}

	emitChange(newPos: number) {
		this.$emit('change', newPos);
	}

	pointermove(ev: PointerEvent) {
		if (this.isDragging) {
			this.emitChange(this.calcPos(ev));
		}
	}

	pointerup(ev: PointerEvent) {
		if (this.isDragging) {
			let frame = this.$refs.frame as HTMLElement;
			frame.releasePointerCapture(ev.pointerId);
			this.isDragging = false;
		}
	}

	pointerdown(ev: PointerEvent) {
		this.isDragging = true;
		let frame = this.$refs.frame as HTMLElement;
		frame.setPointerCapture(ev.pointerId);
		this.emitChange(this.calcPos(ev));
	}

	calcPos(ev: MouseEvent): number {
		let timeline = this.$refs.timeline as HTMLElement;
		let tr = timeline.getBoundingClientRect();
		let p = (ev.clientX - tr.left) / tr.width;
		p = Math.max(0, Math.min(p, 1));
		return p;
	}
}
</script>

<style lang="scss" scoped>
.scrollerFrame {
	width: 100%;
	// margin: 0 50px;
	box-sizing: border-box;
	height: 50px;
	//border-radius: 5px;
	//border: solid 1px #000;
	//background-color: #e1e;
	display: flex;
	flex-direction: column;
	align-items: center;
	justify-content: center;
	position: relative;
	cursor: pointer;
	user-select: none;
}
.timeline {
	box-sizing: border-box;
	//margin: 0 30px;
	width: 90%;
	height: 7px;
	border-radius: 5px;
	border: solid 1px rgba(0, 0, 0, 0.6);
	background-color: rgba(200, 200, 200, 0.25);
	position: relative;
}
.dotFrame {
	width: 55px; // must equal dotWidth
	height: 55px;
	margin-left: -27px;
	top: -25px;
	position: absolute;
	cursor: pointer;
	display: flex;
	justify-content: center;
	align-items: center;
	//background: #0dd;
}
.dot {
	width: 13px;
	height: 13px;
	border-radius: 100px;
	background-color: rgba(255, 255, 255, 0.6);
	border: solid 1px rgba(0, 0, 0, 0.3);
	display: flex;
	justify-content: center;
	align-items: center;
}
.dotdot {
	width: 3px;
	height: 3px;
	border-radius: 10px;
	background-color: rgba(0, 0, 0, 0.7);
}
</style>
