<template>
	<div class='svgOuter' @click='$emit("click")'>
		<div :style='innerStyle'></div>
	</div>
</template>

<script lang="ts">
import { Prop, Watch, Component, Vue } from 'vue-property-decorator';

@Component({})
export default class SvgButton extends Vue {
	@Prop({ default: '' }) icon!: string;

	get innerStyle(): any {
		return {
			'width': '24px',
			'height': '24px',
			'background': 'url("' + this.icon + '")',
			'background-size': 'contain',
			'background-repeat': 'no-repeat',
			'cursor': 'pointer',
		};
	}
}
</script>

<style lang="scss" scoped>
.svgOuter {
	background-color: #eee;
	border-radius: 3px;
	border: solid 1px #bbb;
	padding: 2px;
	cursor: pointer;
	box-shadow: 1px 1px 5px rgba(0, 0, 0, 0.2);
}
.svgOuter:hover {
	background-color: #fff;
	border: solid 1px #777;
}
</style>
