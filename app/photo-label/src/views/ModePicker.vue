<template>
  <div class='outer'>
	<div class='inner'>
		<h1 style='margin-bottom: 10px'>Your name</h1>
		<input v-model='author' class='author' />
		<h1 style='margin-bottom: 10px'>Choose the type of labels</h1>
		<div v-for='dim in dimensions.dims' :key='dim.id' style='font-size: 1rem; display: flex; align-items: center'>
			<button @click='btnClick(dim)'>
				<span style='font-size: 1.3rem'>{{dim.niceName}}: </span>
				<span style='font-size: 0.9rem'>{{dim.explain}}</span>
				<span style='font-size: 0.9rem; color: #281; margin-left: 1em'>{{dim.niceValues}}</span>
			</button>
		</div>
	</div>
  </div>
</template>

<script lang="ts">
import { Component, Vue } from 'vue-property-decorator';
import { DimensionSet, Dimension } from '@/label';
// import HelloWorld from '@/components/HelloWorld.vue'; // @ is an alias to /src

@Component({
})
export default class ModePicker extends Vue {
	dimensions: DimensionSet = new DimensionSet();
	author: string = '';

	btnClick(dim: Dimension) {
		this.author = this.author.trim();
		if (this.author === '') {
			alert('You must enter your name');
			return;
		}

		localStorage.setItem('author', this.author);
		this.$router.push({ name: 'label', params: { dimid: dim.id } });
	}

	mounted() {
		this.author = localStorage.getItem('author') || '';
		DimensionSet.fetch().then((dset: DimensionSet) => {
			this.dimensions = dset;
		});
	}
}
</script>

<style lang="scss" scoped>
.outer {
	display: flex;
	justify-content: center;
}
.inner {
	font-size: 2rem;
	margin: 2em;
	display: flex;
	flex-direction: column;
}
button {
	margin: 5px;
	font-size: 1rem;
	padding: 0.5em 0.6em;
	border-radius: 3px;
	border: solid 1px #aaa;
	box-shadow: 1px 1px 5px rgba(0, 0, 0, 0.1);
	background: #eee;
	color: #000;
	text-decoration: none;
	cursor: pointer;
}
button:hover {
	border: solid 1px #000;
	background: #f3f3f3;
	text-decoration: none;
	cursor: pointer;
}
h1 {
	font-size: 30px;
	margin-top: 30px;
	margin-bottom: 10px;
}
.author {
	margin-left: 0.5em;
	font-size: 20px;
	padding: 4px;
}
</style>
